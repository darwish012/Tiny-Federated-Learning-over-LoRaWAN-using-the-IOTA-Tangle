#include "nn.h"
#include <cmath>
#include <cstdint>

#define ADAM_BETA1 0.9f
#define ADAM_BETA2 0.999f
#define ADAM_EPSILON 1e-8f
// You can now safely increase your LR back up!
#define LR 0.01f 
#define LORA_ALPHA 0.5f


void adam_init(AdamState &state) {
  memset(&state, 0, sizeof(AdamState)); // Zero out all m and v arrays
  state.t = 0;
}

inline float relu(float x) { return x > 0 ? x : 0; }
inline float relu_d(float x) { return x > 0 ? 1 : 0; }

// ── INIT (BASE MODEL - FROZEN) ──────────────
void nn_init(NNModel &m) {

  // Layer 1 (8 neurons)
  float w1_init[H1] = {0.2,-0.1,0.3,-0.2,0.1,0.05,-0.3,0.4};
  memcpy(m.W1, w1_init, sizeof(w1_init));

  memset(m.b1, 0, sizeof(m.b1));

  // Layer 2 (8×4)
  float w2_init[H1 * H2] = {
    0.1,0.2,-0.1,0.05,
    -0.2,0.1,0.3,-0.1,
    0.05,-0.2,0.2,0.1,
    0.3,0.1,-0.1,0.2,
    0.2,-0.3,0.1,0.05,
    0.1,0.2,0.1,-0.2,
    -0.1,0.3,0.2,0.1,
    0.2,0.1,-0.2,0.3
  };

  memcpy(m.W2, w2_init, sizeof(w2_init));
  memset(m.b2, 0, sizeof(m.b2));

  float w3_init[H2] = {0.4,-0.3,0.2,-0.1};
  memcpy(m.W3, w3_init, sizeof(w3_init));

  m.b3 = 0;
}

// ── LORA INIT ───────────────────────────────
void lora_init(LoRAAdapter &lora) {
  // Fixed local projection A must be nonzero for LoRA training to work.
  for (int i = 0; i < LORA_RANK * 1; i++)
    lora.A1[i] = 0.1f + 0.01f * i;

  for (int i = 0; i < LORA_RANK * H1; i++)
    lora.A2[i] = 0.1f + 0.01f * i;

  for (int i = 0; i < LORA_RANK * H2; i++)
    lora.A3[i] = 0.1f + 0.01f * i;

  // B matrices start at zero and are the only trainable part.
  memset(lora.B1, 0, sizeof(lora.B1));
  memset(lora.B2, 0, sizeof(lora.B2));
  memset(lora.B3, 0, sizeof(lora.B3));
}


// ── FORWARD (WITH LORA) ──────────────────────
float nn_forward(NNModel &m, LoRAAdapter &lora, Cache &c, float x) {

  // Layer 1: W1 is [8], input is scalar (1D→8)
  for (int i = 0; i < H1; i++) {
    float base = m.W1[i] * x + m.b1[i];
    
    // LoRA contribution: (B1 @ A1)[i] @ x
    float lora_contrib = 0;
    for (int r = 0; r < LORA_RANK; r++) {
      lora_contrib += lora.B1[i * LORA_RANK + r] * lora.A1[r * 1];
    }
    
    c.z1[i] = base + LORA_ALPHA * lora_contrib * x;
    c.a1[i] = relu(c.z1[i]);
  }

  // Layer 2: W2 is [4×8], input is a1[8]
  for (int j = 0; j < H2; j++) {
    float base = m.b2[j];
    float lora_contrib = 0;
    
    for (int i = 0; i < H1; i++) {
      base += m.W2[j * H1 + i] * c.a1[i];
      
      // LoRA: (B2 @ A2)[j][i] @ c.a1[i]
      float ba_contrib = 0;
      for (int r = 0; r < LORA_RANK; r++) {
        ba_contrib += lora.B2[j * LORA_RANK + r] * lora.A2[r * H1 + i];
      }
      lora_contrib += ba_contrib * c.a1[i];
    }
    
    c.z2[j] = base + LORA_ALPHA * lora_contrib;
    c.a2[j] = relu(c.z2[j]);
  }

  // Layer 3: W3 is [1], input is a2[4]
  float out = m.b3;
  float lora_contrib = 0;
  
  for (int j = 0; j < H2; j++) {
    out += m.W3[j] * c.a2[j];
    
    // LoRA: (B3 @ A3)[0][j] @ c.a2[j]
    float ba_contrib = 0;
    for (int r = 0; r < LORA_RANK; r++) {
      ba_contrib += lora.B3[0 * LORA_RANK + r] * lora.A3[r * H2 + j];
    }
    lora_contrib += ba_contrib * c.a2[j];
  }

  out += LORA_ALPHA * lora_contrib;
  return out;
}
// ... (nn_forward is up here) ...

// ── HELPER FUNCTIONS ──────────────────────

// 1. Gradient Clipper (Prevents exploding gradients)
inline float clip(float x, float min_val, float max_val) {
  if (x < min_val) return min_val;
  if (x > max_val) return max_val;
  return x;
}

// 2. Adam Optimizer Logic (Calculates momentum/velocity)
inline void apply_adam(float &weight, float &m, float &v, float grad, uint32_t t) {
  // Update biased first moment estimate
  m = ADAM_BETA1 * m + (1.0f - ADAM_BETA1) * grad;
  // Update biased second raw moment estimate
  v = ADAM_BETA2 * v + (1.0f - ADAM_BETA2) * (grad * grad);

  // Compute bias-corrected first moment estimate
  float m_hat = m / (1.0f - pow(ADAM_BETA1, t));
  // Compute bias-corrected second raw moment estimate
  float v_hat = v / (1.0f - pow(ADAM_BETA2, t));

  // Update weights
  weight -= LR * (m_hat / (sqrt(v_hat) + ADAM_EPSILON));
}

// ── BACKPROP (LORA ONLY) ──────────────────
void nn_backward(NNModel &m, LoRAAdapter &lora, Cache &c, AdamState &adam, float x, float y, float pred) {
  adam.t++; // Increment time step for bias correction
  
  // 1. Calculate raw error
  float dL = 2 * (pred - y);

  // 2. Clip the error to strictly prevent explosions!
  dL = clip(dL, -1.0f, 1.0f);

  float d2[H2];

  // ──── Layer 3 Backprop ────
  for (int j = 0; j < H2; j++) {
    d2[j] = dL * m.W3[j] * relu_d(c.z2[j]);
  }

  for (int r = 0; r < LORA_RANK; r++) {
    float inner = 0;
    for (int j = 0; j < H2; j++) {
      inner += lora.A3[r * H2 + j] * c.a2[j];
    }
    float grad = LORA_ALPHA * dL * inner;
    apply_adam(lora.B3[r], adam.m_B3[r], adam.v_B3[r], grad, adam.t);
  }

  // ──── Layer 2 Backprop ────
  for (int j = 0; j < H2; j++) {
    for (int r = 0; r < LORA_RANK; r++) {
      float inner = 0;
      for (int i = 0; i < H1; i++) {
        inner += lora.A2[r * H1 + i] * c.a1[i];
      }
      float grad = LORA_ALPHA * d2[j] * inner;
      int idx = j * LORA_RANK + r;
      apply_adam(lora.B2[idx], adam.m_B2[idx], adam.v_B2[idx], grad, adam.t);
    }
  }

  // ──── Layer 1 Backprop ────
  for (int i = 0; i < H1; i++) {
    float d1 = 0;
    for (int j = 0; j < H2; j++) {
      d1 += d2[j] * m.W2[j * H1 + i];
    }
    d1 *= relu_d(c.z1[i]);

    for (int r = 0; r < LORA_RANK; r++) {
      float grad = LORA_ALPHA * d1 * lora.A1[r] * x;
      int idx = i * LORA_RANK + r;
      apply_adam(lora.B1[idx], adam.m_B1[idx], adam.v_B1[idx], grad, adam.t);
    }
  }
}

// ── SERIALIZATION (B MATRICES ONLY) ─────────
void lora_pack(LoRAAdapter &lora, uint8_t *buf) {
  int off = 0;
  auto cp = [&](void *s, int n) {
    memcpy(buf + off, s, n);
    off += n;
  };

  // Only pack B matrices (A stay local)
  cp(lora.B1, sizeof(lora.B1));
  cp(lora.B2, sizeof(lora.B2));
  cp(lora.B3, sizeof(lora.B3));
}

void lora_unpack(LoRAAdapter &lora, uint8_t *buf) {
  int off = 0;
  auto cp = [&](void *d, int n) {
    memcpy(d, buf + off, n);
    off += n;
  };

  // Only unpack B matrices (A stay local)
  cp(lora.B1, sizeof(lora.B1));
  cp(lora.B2, sizeof(lora.B2));
  cp(lora.B3, sizeof(lora.B3));
}

