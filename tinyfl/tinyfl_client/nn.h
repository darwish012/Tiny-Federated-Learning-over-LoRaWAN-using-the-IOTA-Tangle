#pragma once
#include <Arduino.h>

#define INPUT_SIZE 50  // 50-value feature array
#define H1 8
#define H2 4
#define OUTPUT_SIZE 4  // 4 classes: dog, cat, horse, other
#define LORA_RANK 4

struct NNModel {
  float W1[H1 * INPUT_SIZE];      // 400 floats (frozen)
  float b1[H1];
  float W2[H1 * H2];              // 32
  float b2[H2];
  float W3[OUTPUT_SIZE * H2];     // 16 (4 outputs × 4 inputs)
  float b3[OUTPUT_SIZE];           // 4 biases
};

struct LoRAAdapter {
  float A1[LORA_RANK * INPUT_SIZE]; // 200 (frozen)
  float A2[LORA_RANK * H1];         // 32  (frozen)
  float A3[LORA_RANK * H2];         // 16  (frozen)

  // B matrices: 32 + 16 + 16 = 64 trainable parameters
  float B1[H1 * LORA_RANK];           // 32
  float B2[H2 * LORA_RANK];           // 16
  float B3[OUTPUT_SIZE * LORA_RANK];   // 16
};

struct Cache {
  float z1[H1], a1[H1];
  float z2[H2], a2[H2];
  float z3[OUTPUT_SIZE];     // pre-softmax logits
  float a3[OUTPUT_SIZE];     // post-softmax probabilities
  float a1_lora_in[LORA_RANK];
};

struct AdamState {
  float m_B1[H1 * LORA_RANK]; float v_B1[H1 * LORA_RANK];
  float m_B2[H2 * LORA_RANK]; float v_B2[H2 * LORA_RANK];
  float m_B3[OUTPUT_SIZE * LORA_RANK]; float v_B3[OUTPUT_SIZE * LORA_RANK];
  uint32_t t;
};

void adam_init(AdamState &state);
void nn_init(NNModel &m);
void lora_init(LoRAAdapter &lora);

// Forward: returns predicted class (0-3), stores probs in cache.a3
int nn_forward(NNModel &m, LoRAAdapter &lora, Cache &c, float *x);
// Backward: cross-entropy loss, updates B matrices only
void nn_backward(NNModel &m, LoRAAdapter &lora, Cache &c, AdamState &adam, float *x, int y_class);
void lora_pack(LoRAAdapter &lora, uint8_t *buf);
void lora_unpack(LoRAAdapter &lora, uint8_t *buf);