#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <math.h>

#include "nn.h"

// ── SENSOR ─────────────────────────────
#define DHTPIN   14
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// ── BLE UUIDs ──────────────────────────
#define SERVICE_UUID     "12345678-1234-1234-1234-123456789abc"
#define WEIGHT_CHAR_UUID "12345678-1234-1234-1234-123456789001"
#define GLOBAL_CHAR_UUID "12345678-1234-1234-1234-123456789002"

// ── TIMING ─────────────────────────────
#define SAMPLE_MS    2000
#define TRAIN_MS     20000
#define BUFFER_SIZE  30

// ── CLASS NAMES ────────────────────────
const char* CLASS_NAMES[] = {"dog", "cat", "horse", "other"};

// ── MODEL + CACHE ──────────────────────
static NNModel model;
static LoRAAdapter lora;
static Cache cache;
static AdamState adam;

// ── DATA BUFFER ────────────────────────
static float featureBuf[BUFFER_SIZE][INPUT_SIZE];
static int   labelBuf[BUFFER_SIZE];
static int   bufIdx = 0;
static int   sampleCount = 0;

// ── VALIDATION TRACKING ─────────────────
static float valFeatureBuf[10][INPUT_SIZE];
static int   valLabelBuf[10];
static int   valIdx = 0;
static int   valCount = 0;
static float lastValAccuracy = 0.0f;

// ── BLE BUFFER ─────────────────────────
// B1(32) + B2(16) + B3(16) = 64 bytes + 4 (int) + 4 (float) = 72
static uint8_t bleBuffer[216];

// ── BLE OBJECTS ────────────────────────
BLEServer* pServer = nullptr;
BLECharacteristic* pWeightChar = nullptr;
BLECharacteristic* pGlobalChar = nullptr;
bool connected = false;

// ── TIMERS ─────────────────────────────
uint32_t lastSample = 0;
uint32_t lastTrain = 0;

// ── BALANCED RANDOM PROJECTION ──────────
static float projection_P[4][INPUT_SIZE];

void init_projection_weights() {
  uint32_t seed = 42;
  for (int c = 0; c < 4; c++) {
    float sum = 0.0f;
    for (int k = 0; k < INPUT_SIZE; k++) {
      seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
      projection_P[c][k] = ((float)seed / (float)0x7FFFFFFF - 0.5f);
      sum += projection_P[c][k];
    }
    float mean = sum / (float)INPUT_SIZE;
    for (int k = 0; k < INPUT_SIZE; k++) {
      projection_P[c][k] -= mean;
    }
  }
}

// ── BUFFER HELP ─────────────────────────
void pushSample(float *x, int y_class) {
  for (int k = 0; k < INPUT_SIZE; k++) {
    featureBuf[bufIdx][k] = x[k];
  }
  labelBuf[bufIdx] = y_class;

  bufIdx = (bufIdx + 1) % BUFFER_SIZE;
  if (sampleCount < BUFFER_SIZE) sampleCount++;

  for (int k = 0; k < INPUT_SIZE; k++) {
    valFeatureBuf[valIdx][k] = x[k];
  }
  valLabelBuf[valIdx] = y_class;

  valIdx = (valIdx + 1) % 10;
  if (valCount < 10) valCount++;
}

// ── VALIDATION ACCURACY ──────────────────
float calculateValidationAccuracy() {
  if (valCount < 1) return 0.0f;

  int correct = 0;
  for (int i = 0; i < valCount; i++) {
    int idx = (valIdx - valCount + i + 10) % 10;
    float* x = valFeatureBuf[idx];
    int y_true = valLabelBuf[idx];
    int y_pred = nn_forward(model, lora, cache, x);
    if (y_pred == y_true) correct++;
  }

  return (float)correct / valCount;
}

// ── TRAINING ────────────────────────────
void localTrain() {
  if (sampleCount < 1) return;

  Serial.println("\n[FL] Local training (4-class cross-entropy)");

  for (int epoch = 0; epoch < 15; epoch++) {
    float loss = 0;
    int correct = 0;

    for (int i = 0; i < sampleCount; i++) {
      int idx = (bufIdx - sampleCount + i + BUFFER_SIZE) % BUFFER_SIZE;
      float* x = featureBuf[idx];
      int y_class = labelBuf[idx];

      int pred_class = nn_forward(model, lora, cache, x);

      // Cross-entropy loss for logging
      float prob = cache.a3[y_class];
      if (prob < 1e-7f) prob = 1e-7f;
      loss += -logf(prob);

      if (pred_class == y_class) correct++;

      nn_backward(model, lora, cache, adam, x, y_class);
    }

    float acc = (float)correct / sampleCount * 100.0f;
    Serial.printf("  Epoch %2d  Loss=%.4f  Acc=%.1f%%\n",
                  epoch, loss / sampleCount, acc);
  }

  float valAcc = calculateValidationAccuracy();
  lastValAccuracy = valAcc;
  Serial.printf("[ACCURACY] Validation: %.1f%% (n=%d)\n",
                valAcc * 100.0f, valCount);
}

// ── PACK LORA B ─────────────────────────
int packWeights() {
  // Debug print stats before packing
  float min_val = lora.B1[0], max_val = lora.B1[0], sum = 0.0f;
  for (int i = 0; i < H1 * LORA_RANK; i++) {
    if (lora.B1[i] < min_val) min_val = lora.B1[i];
    if (lora.B1[i] > max_val) max_val = lora.B1[i];
    sum += lora.B1[i];
  }
  Serial.printf("[FL] B1 pre-pack: min=%.4f, max=%.4f, mean=%.4f\n", min_val, max_val, sum / (H1 * LORA_RANK));

  lora_pack(lora, bleBuffer);

  int payloadSize = 64;  // 32 + 16 + 16

  memcpy(bleBuffer + payloadSize, &sampleCount, sizeof(int));
  payloadSize += sizeof(int);

  memcpy(bleBuffer + payloadSize, &lastValAccuracy, sizeof(float));
  payloadSize += sizeof(float);

  // Total: 72 bytes
  return payloadSize;
}

// ── UNPACK LORA ────────────────────────
void unpackWeights(uint8_t* buf) {
  lora_unpack(lora, buf);
  adam_init(adam);  // Reset optimizer for new global weights
  Serial.println("[FL] Global model updated (Adam reset)");
  
  // Debug print stats to verify unpacked weights
  float min_val = lora.B1[0], max_val = lora.B1[0], sum = 0.0f;
  for (int i = 0; i < H1 * LORA_RANK; i++) {
    if (lora.B1[i] < min_val) min_val = lora.B1[i];
    if (lora.B1[i] > max_val) max_val = lora.B1[i];
    sum += lora.B1[i];
  }
  Serial.printf("[FL] B1 unpacked: min=%.4f, max=%.4f, mean=%.4f\n", min_val, max_val, sum / (H1 * LORA_RANK));
}

// ── BLE CALLBACKS ──────────────────────
class ConnCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    Serial.println("[BLE] Connected");
  }
  void onDisconnect(BLEServer*) override {
    connected = false;
    Serial.println("[BLE] Disconnected");
    BLEDevice::startAdvertising();
  }
};

class GlobalCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String value = c->getValue();
    Serial.printf("[BLE] Received global model (%d bytes)\n", value.length());
    if (value.length() >= 64) {
      unpackWeights((uint8_t*)value.c_str());
    } else {
      Serial.println("[BLE] ERROR: Received truncated payload, update aborted!");
    }
  }
};

// ── SEND WEIGHTS ───────────────────────
void sendWeights() {
  if (!connected) return;
  int payloadSize = packWeights();
  pWeightChar->setValue(bleBuffer, payloadSize);
  pWeightChar->notify();
  Serial.printf("[FL] Model sent (%d bytes)\n", payloadSize);
}

// ── SETUP ──────────────────────────────
void setup() {
  Serial.begin(115200);
  randomSeed(42);
  dht.begin();

  nn_init(model);
  lora_init(lora);
  adam_init(adam);
  init_projection_weights();

  BLEDevice::init("TinyFL-Node");
  BLEDevice::setMTU(256); // Set MTU size to allow larger packets
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ConnCB());

  BLEService* svc = pServer->createService(SERVICE_UUID);

  pWeightChar = svc->createCharacteristic(
    WEIGHT_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pWeightChar->addDescriptor(new BLE2902());

  pGlobalChar = svc->createCharacteristic(
    GLOBAL_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pGlobalChar->setCallbacks(new GlobalCB());

  svc->start();
  BLEDevice::startAdvertising();

  Serial.println("[SYSTEM] TinyFL Ready (4-class: dog/cat/horse/other)");
}

// ── LOOP ───────────────────────────────
void loop() {
  uint32_t now = millis();

  // Sampling
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    float mock_x[INPUT_SIZE];
    for (int k = 0; k < INPUT_SIZE; k++) {
      mock_x[k] = (rand() % 100) / 100.0f;
    }

    // Balanced projection classification using all 50 features
    float scores[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int c = 0; c < 4; c++) {
      for (int k = 0; k < INPUT_SIZE; k++) {
        scores[c] += mock_x[k] * projection_P[c][k];
      }
    }
    int mock_class = 0;
    for (int c = 1; c < 4; c++) {
      if (scores[c] > scores[mock_class]) {
        mock_class = c;
      }
    }

    pushSample(mock_x, mock_class);
    Serial.printf("[DATA] class=%s (n=%d)\n", CLASS_NAMES[mock_class], sampleCount);
  }

  // Training + FL send
  if (now - lastTrain >= TRAIN_MS) {
    lastTrain = now;
    localTrain();
    sendWeights();
  }
}