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
// ── SYNTHETIC DATA ─────────────────────
#define USE_SYNTHETIC_DATA 1
// ── NORMALIZATION ──────────────────────
#define TEMP_MEAN 25.0f
#define TEMP_STD   5.0f

inline float norm(float t) { return (t - TEMP_MEAN) / TEMP_STD; }
inline float denorm(float n) { return n * TEMP_STD + TEMP_MEAN; }

// ── MODEL + CACHE ──────────────────────

// ── DATA BUFFER ────────────────────────
static float tempBuf[BUFFER_SIZE];
static int bufIdx = 0;
static int sampleCount = 0;

// ── VALIDATION TRACKING ─────────────────
static float valBuf[10];  // Keep last 10 samples for validation
static int valIdx = 0;
static int valCount = 0;
static float runningMSE = 0.0f;
static int mseCount = 0;
static float lastValMSE = 0.0f;
static NNModel model;
static LoRAAdapter lora;
static Cache cache;
static AdamState adam; // NEW: Add Adam state
// ── BLE BUFFER ─────────────────────────
// LoRA B-only rank-4: B1(32) + B2(16) + B3(4) = 52 floats + 1 int + 1 float = 216 bytes
static uint8_t bleBuffer[216];

// ── BLE OBJECTS ────────────────────────
BLEServer* pServer = nullptr;
BLECharacteristic* pWeightChar = nullptr;
BLECharacteristic* pGlobalChar = nullptr;
bool connected = false;

// ── TIMERS ─────────────────────────────
uint32_t lastSample = 0;
uint32_t lastTrain = 0;
static int synthStep = 0;

// ── SYNTHETIC FUNCTION ──────────────────
float target_function(float t) {
  return 25.0f
    + 2.0f * sin(0.5f * t)
    + 1.2f * sin(1.3f * t + 0.5f)
    + 0.8f * cos(0.25f * t)
    + 0.03f * t
    - 0.002f * t * t;
}

float generateSyntheticSample() {
  float t = synthStep * 0.5f;
  float value = target_function(t);
  float noise = (random(-20, 21) / 100.0f);
  synthStep++;
  return value + noise;
}

// ── BUFFER HELP ─────────────────────────
void pushSample(float t) {
  tempBuf[bufIdx] = t;
  bufIdx = (bufIdx + 1) % BUFFER_SIZE;
  if (sampleCount < BUFFER_SIZE) sampleCount++;

  // Also add to validation buffer
  valBuf[valIdx] = t;
  valIdx = (valIdx + 1) % 10;
  if (valCount < 10) valCount++;
}

// ── VALIDATION ACCURACY ──────────────────
float calculateValidationMSE() {
  if (valCount < 2) return 0.0f;

  float mse = 0.0f;
  int count = 0;

  for (int i = 0; i < valCount - 1; i++) {
    int currIdx = (valIdx - valCount + i + 10) % 10;
    int nextIdx = (currIdx + 1) % 10;

    float x = norm(valBuf[currIdx]);
    float y_true = norm(valBuf[nextIdx]);
    float y_pred = nn_forward(model, lora, cache, x);

    float error = y_pred - y_true;
    mse += error * error;
    count++;
  }

  return count > 0 ? mse / count : 0.0f;
}

// ── TRAINING ────────────────────────────
void localTrain() {
  if (sampleCount < 2) return;

  int pairs = sampleCount - 1;

  Serial.println("\n[FL] Local LoRA training started");

  for (int epoch = 0; epoch < 5; epoch++) {
    float mse = 0;

    for (int i = 0; i < pairs; i++) {
      int xi = (bufIdx - pairs - 1 + i + BUFFER_SIZE) % BUFFER_SIZE;
      int yi = (xi + 1) % BUFFER_SIZE;

      float x = norm(tempBuf[xi]);
      float y = norm(tempBuf[yi]);

      float pred = nn_forward(model, lora, cache, x);
      mse += (pred - y) * (pred - y);

      nn_backward(model, lora, cache, adam, x, y, pred);
    }

    Serial.printf("Epoch %d MSE=%.5f\n", epoch, mse / pairs);
  }

  // Calculate and log validation accuracy
  float valMSE = calculateValidationMSE();
  lastValMSE = valMSE;
  Serial.printf("[ACCURACY] Validation MSE: %.5f (n=%d)\n", valMSE, valCount);
}

// ── PACK LORA B ─────────────────────────
int packWeights() {
  lora_pack(lora, bleBuffer);
  int payloadSize = 52 * 4; // 52 floats
  
  // Pack sample count
  memcpy(bleBuffer + payloadSize, &sampleCount, sizeof(int));
  payloadSize += sizeof(int);

  // Pack validation MSE
  memcpy(bleBuffer + payloadSize, &lastValMSE, sizeof(float));
  payloadSize += sizeof(float);
  
  return payloadSize;
}

// ── UNPACK LORA ────────────────────────
void unpackWeights(uint8_t* buf) {
  lora_unpack(lora, buf);
  Serial.println("[FL] Global LoRA model updated");
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
    unpackWeights((uint8_t*)c->getData());
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
  adam_init(adam); // NEW: Initialize Adam
  BLEDevice::init("TinyFL-Node");

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

  Serial.println("[SYSTEM] TinyFL Ready (LoRA rank-4, 1→8→4→1)");
}

// ── LOOP ───────────────────────────────
void loop() {
  uint32_t now = millis();

  // sampling
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    float t;
#if USE_SYNTHETIC_DATA
    t = generateSyntheticSample();
#else
    t = dht.readTemperature();
#endif

    if (!isnan(t)) {
      pushSample(t);
      Serial.printf("[DATA] Temp %.2f (n=%d)\n", t, sampleCount);
    }
  }

  // training + FL send
  if (now - lastTrain >= TRAIN_MS) {
    lastTrain = now;

    localTrain();
    sendWeights();
  }
}