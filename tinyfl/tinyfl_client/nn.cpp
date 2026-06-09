#include "nn.h"
#include <cmath>
#include <cstdint>

#define ADAM_BETA1 0.9f
#define ADAM_BETA2 0.999f
#define ADAM_EPSILON 1e-8f
#define ADAM_WEIGHT_DECAY 0.001f
#define LR 0.02f
#define LORA_ALPHA 0.5f
#define QUANT_SCALE 0.1f

void adam_init(AdamState &state) {
  memset(&state, 0, sizeof(AdamState));
  state.t = 0;
}

inline float relu(float x) { return x > 0 ? x : 0; }
inline float relu_d(float x) { return x > 0 ? 1 : 0; }

// ── INIT (BASE MODEL - FROZEN) ──────────────
void nn_init(NNModel &m) {
  for (int i = 0; i < H1 * INPUT_SIZE; i++) {
    m.W1[i] = sin(i * 0.1f) * 0.1f;
  }
  memset(m.b1, 0, sizeof(m.b1));

  float w2_init[H1 * H2] = {
    0.1, 0.2, -0.1, 0.05,
    -0.2, 0.1, 0.3, -0.1,
    0.05, -0.2, 0.2, 0.1,
    0.3, 0.1, -0.1, 0.2,
    0.2, -0.3, 0.1, 0.05,
    0.1, 0.2, 0.1, -0.2,
    -0.1, 0.3, 0.2, 0.1,
    0.2, 0.1, -0.2, 0.3
  };
  memcpy(m.W2, w2_init, sizeof(w2_init));
  memset(m.b2, 0, sizeof(m.b2));

  // W3: OUTPUT_SIZE × H2 = 4×4 = 16 values
  float w3_init[OUTPUT_SIZE * H2] = {
     0.10, -0.15,  0.05,  0.20,   // class 0 (dog)
    -0.10,  0.20, -0.05, -0.15,   // class 1 (cat)
     0.15, -0.10,  0.20, -0.05,   // class 2 (horse)
    -0.05,  0.05, -0.20,  0.10    // class 3 (other)
  };
  memcpy(m.W3, w3_init, sizeof(w3_init));
  memset(m.b3, 0, sizeof(m.b3));
}

// ── LORA INIT ───────────────────────────────
void lora_init(LoRAAdapter &lora) {
  // Deterministic LCG — MUST match Python server's _lcg_init()
  uint32_t seed = 42;

  for (int i = 0; i < LORA_RANK * INPUT_SIZE; i++) {
    seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
    lora.A1[i] = ((float)seed / (float)0x7FFFFFFF - 0.5f) * 0.4f;
  }
  for (int i = 0; i < LORA_RANK * H1; i++) {
    seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
    lora.A2[i] = ((float)seed / (float)0x7FFFFFFF - 0.5f) * 0.4f;
  }
  for (int i = 0; i < LORA_RANK * H2; i++) {
    seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
    lora.A3[i] = ((float)seed / (float)0x7FFFFFFF - 0.5f) * 0.4f;
  }

  memset(lora.B1, 0, sizeof(lora.B1));
  memset(lora.B2, 0, sizeof(lora.B2));
  memset(lora.B3, 0, sizeof(lora.B3));
}

// ── FORWARD (LORA + SOFTMAX) ─────────────────
int nn_forward(NNModel &m, LoRAAdapter &lora, Cache &c, float *x) {

  // Pre-calculate A1 @ x
  for (int r = 0; r < LORA_RANK; r++) {
    c.a1_lora_in[r] = 0.0f;
    for (int k = 0; k < INPUT_SIZE; k++) {
      c.a1_lora_in[r] += lora.A1[r * INPUT_SIZE + k] * x[k];
    }
  }

  // Layer 1: INPUT_SIZE -> H1
  for (int i = 0; i < H1; i++) {
    float base = m.b1[i];
    for (int k = 0; k < INPUT_SIZE; k++) {
      base += m.W1[i * INPUT_SIZE + k] * x[k];
    }
    float lora_c = 0;
    for (int r = 0; r < LORA_RANK; r++) {
      lora_c += lora.B1[i * LORA_RANK + r] * c.a1_lora_in[r];
    }
    c.z1[i] = base + LORA_ALPHA * lora_c;
    c.a1[i] = relu(c.z1[i]);
  }

  // Layer 2: H1 -> H2
  for (int j = 0; j < H2; j++) {
    float base = m.b2[j];
    float lora_c = 0;
    for (int i = 0; i < H1; i++) {
      base += m.W2[j * H1 + i] * c.a1[i];
      float ba = 0;
      for (int r = 0; r < LORA_RANK; r++) {
        ba += lora.B2[j * LORA_RANK + r] * lora.A2[r * H1 + i];
      }
      lora_c += ba * c.a1[i];
    }
    c.z2[j] = base + LORA_ALPHA * lora_c;
    c.a2[j] = relu(c.z2[j]);
  }

  // Layer 3: H2 -> OUTPUT_SIZE (4 classes)
  for (int o = 0; o < OUTPUT_SIZE; o++) {
    float base = m.b3[o];
    float lora_c = 0;
    for (int j = 0; j < H2; j++) {
      base += m.W3[o * H2 + j] * c.a2[j];
      float ba = 0;
      for (int r = 0; r < LORA_RANK; r++) {
        ba += lora.B3[o * LORA_RANK + r] * lora.A3[r * H2 + j];
      }
      lora_c += ba * c.a2[j];
    }
    c.z3[o] = base + LORA_ALPHA * lora_c;
  }

  // Softmax (numerically stable)
  float max_z = c.z3[0];
  for (int o = 1; o < OUTPUT_SIZE; o++)
    if (c.z3[o] > max_z) max_z = c.z3[o];

  float sum_exp = 0;
  for (int o = 0; o < OUTPUT_SIZE; o++) {
    c.a3[o] = expf(c.z3[o] - max_z);
    sum_exp += c.a3[o];
  }
  for (int o = 0; o < OUTPUT_SIZE; o++)
    c.a3[o] /= sum_exp;

  // Argmax → predicted class
  int pred = 0;
  for (int o = 1; o < OUTPUT_SIZE; o++)
    if (c.a3[o] > c.a3[pred]) pred = o;

  return pred;
}

// ── HELPERS ──────────────────────────────────
inline float clip(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

inline void apply_adamw(float &weight, float &m, float &v, float grad, uint32_t t) {
  m = ADAM_BETA1 * m + (1.0f - ADAM_BETA1) * grad;
  v = ADAM_BETA2 * v + (1.0f - ADAM_BETA2) * (grad * grad);
  float m_hat = m / (1.0f - pow(ADAM_BETA1, t));
  float v_hat = v / (1.0f - pow(ADAM_BETA2, t));
  weight -= LR * (m_hat / (sqrt(v_hat) + ADAM_EPSILON) + ADAM_WEIGHT_DECAY * weight);
}

// ── BACKPROP (CROSS-ENTROPY + SOFTMAX) ───────
void nn_backward(NNModel &m, LoRAAdapter &lora, Cache &c, AdamState &adam, float *x, int y_class) {
  adam.t++;

  // Cross-entropy + softmax gradient: d3[o] = a3[o] - 1(o == y_class)
  float d3[OUTPUT_SIZE];
  for (int o = 0; o < OUTPUT_SIZE; o++) {
    d3[o] = c.a3[o] - (o == y_class ? 1.0f : 0.0f);
  }

  // ──── Layer 3: Update B3 ────
  for (int o = 0; o < OUTPUT_SIZE; o++) {
    for (int r = 0; r < LORA_RANK; r++) {
      float inner = 0;
      for (int j = 0; j < H2; j++) {
        inner += lora.A3[r * H2 + j] * c.a2[j];
      }
      float grad = clip(LORA_ALPHA * d3[o] * inner, -1.0f, 1.0f);
      int idx = o * LORA_RANK + r;
      apply_adamw(lora.B3[idx], adam.m_B3[idx], adam.v_B3[idx], grad, adam.t);
    }
  }

  // ──── Backprop d3 → d2 ────
  float d2[H2];
  for (int j = 0; j < H2; j++) {
    float sum = 0;
    for (int o = 0; o < OUTPUT_SIZE; o++) {
      float eff_w3 = m.W3[o * H2 + j];
      for (int r = 0; r < LORA_RANK; r++) {
        eff_w3 += LORA_ALPHA * lora.B3[o * LORA_RANK + r] * lora.A3[r * H2 + j];
      }
      sum += d3[o] * eff_w3;
    }
    d2[j] = sum * relu_d(c.z2[j]);
  }

  // ──── Layer 2: Update B2 ────
  for (int j = 0; j < H2; j++) {
    for (int r = 0; r < LORA_RANK; r++) {
      float inner = 0;
      for (int i = 0; i < H1; i++) {
        inner += lora.A2[r * H1 + i] * c.a1[i];
      }
      float grad = clip(LORA_ALPHA * d2[j] * inner, -1.0f, 1.0f);
      int idx = j * LORA_RANK + r;
      apply_adamw(lora.B2[idx], adam.m_B2[idx], adam.v_B2[idx], grad, adam.t);
    }
  }

  // ──── Layer 1: Update B1 ────
  for (int i = 0; i < H1; i++) {
    float d1 = 0;
    for (int j = 0; j < H2; j++) {
      float eff_w2 = m.W2[j * H1 + i];
      for (int r = 0; r < LORA_RANK; r++) {
        eff_w2 += LORA_ALPHA * lora.B2[j * LORA_RANK + r] * lora.A2[r * H1 + i];
      }
      d1 += d2[j] * eff_w2;
    }
    d1 *= relu_d(c.z1[i]);

    for (int r = 0; r < LORA_RANK; r++) {
      float grad = clip(LORA_ALPHA * d1 * c.a1_lora_in[r], -1.0f, 1.0f);
      int idx = i * LORA_RANK + r;
      apply_adamw(lora.B1[idx], adam.m_B1[idx], adam.v_B1[idx], grad, adam.t);
    }
  }
}

// ── SERIALIZATION (INT8 QUANTIZATION) ─────────
inline int8_t quantize_weight(float w) {
  float scaled = w * QUANT_SCALE * 127.0f;
  if (scaled > 127.0f) return 127;
  if (scaled < -128.0f) return -128;
  return (int8_t)round(scaled);
}

void lora_pack(LoRAAdapter &lora, uint8_t *buf) {
  int idx = 0;
  auto pack_mat = [&](float *mat, int size) {
    for (int i = 0; i < size; i++) {
      buf[idx++] = (uint8_t)quantize_weight(mat[i]);
    }
  };
  pack_mat(lora.B1, H1 * LORA_RANK);           // 32
  pack_mat(lora.B2, H2 * LORA_RANK);           // 16
  pack_mat(lora.B3, OUTPUT_SIZE * LORA_RANK);   // 16
}

void lora_unpack(LoRAAdapter &lora, uint8_t *buf) {
  int idx = 0;
  auto unpack_mat = [&](float *mat, int size) {
    for (int i = 0; i < size; i++) {
      int8_t q_val = (int8_t)buf[idx++];
      mat[i] = (float)q_val / (127.0f * QUANT_SCALE);
    }
  };
  unpack_mat(lora.B1, H1 * LORA_RANK);
  unpack_mat(lora.B2, H2 * LORA_RANK);
  unpack_mat(lora.B3, OUTPUT_SIZE * LORA_RANK);
}
