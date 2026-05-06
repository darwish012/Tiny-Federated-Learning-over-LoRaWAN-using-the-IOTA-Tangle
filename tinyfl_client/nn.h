#pragma once
#include <Arduino.h>   // gives uint8_t, float types, etc.
#define H1 8
#define H2 4
#define LORA_RANK 4

struct NNModel {
  float W1[H1];
  float b1[H1];

  float W2[H1 * H2];
  float b2[H2];

  float W3[H2];
  float b3;
};

// LoRA adapters: A @ B ≈ weight update
struct LoRAAdapter {
  // A matrices stay LOCAL (not sent)
  float A1[LORA_RANK * 1];
  float A2[LORA_RANK * H1];
  float A3[LORA_RANK * H2];
  
  // B matrices get aggregated (sent)
  float B1[H1 * LORA_RANK];
  float B2[H2 * LORA_RANK];
  float B3[1 * LORA_RANK];
};
// Adam Optimizer State
struct AdamState {
  float m_B1[H1 * LORA_RANK]; float v_B1[H1 * LORA_RANK];
  float m_B2[H2 * LORA_RANK]; float v_B2[H2 * LORA_RANK];
  float m_B3[1 * LORA_RANK];  float v_B3[1 * LORA_RANK];
  uint32_t t; // Step counter
};

// Add to your function declarations at the bottom:
void adam_init(AdamState &state);
struct Cache {
  float z1[H1], a1[H1];
  float z2[H2], a2[H2];
  
  // Cache for LoRA intermediate computations
  float lora_z1[LORA_RANK];
};

void nn_init(NNModel &m);
void lora_init(LoRAAdapter &lora);

float nn_forward(NNModel &m, LoRAAdapter &lora, Cache &c, float x);

void nn_backward(NNModel &m, LoRAAdapter &lora, Cache &c, AdamState &adam, float x, float y, float pred);
void lora_pack(LoRAAdapter &lora, uint8_t *buf);
void lora_unpack(LoRAAdapter &lora, uint8_t *buf);