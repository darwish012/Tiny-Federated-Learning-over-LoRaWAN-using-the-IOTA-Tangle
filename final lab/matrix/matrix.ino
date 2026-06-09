#define RED_LED 25
#define BLUE_LED 2

#define MATRIX_SIZE 40

float matrixA[MATRIX_SIZE][MATRIX_SIZE];
float matrixB[MATRIX_SIZE][MATRIX_SIZE];
float matrixResult[MATRIX_SIZE][MATRIX_SIZE];

void setup() {

  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(BLUE_LED, LOW);

  // Initialize matrices
  for (int i = 0; i < MATRIX_SIZE; i++) {
    for (int j = 0; j < MATRIX_SIZE; j++) {
      matrixA[i][j] = i + j;
      matrixB[i][j] = i * j;
    }
  }
}

void heavyLoad(uint32_t durationMs) {

  uint32_t startTime = millis();

  // LEDs ON = high power phase
  digitalWrite(RED_LED, HIGH);
  digitalWrite(BLUE_LED, HIGH);

  // Heavy CPU load
  while (millis() - startTime < durationMs) {

    for (int i = 0; i < MATRIX_SIZE; i++) {
      for (int j = 0; j < MATRIX_SIZE; j++) {

        float sum = 0;

        for (int k = 0; k < MATRIX_SIZE; k++) {
          sum += matrixA[i][k] * matrixB[k][j];
        }

        matrixResult[i][j] = sum;
      }
    }
  }

  // LEDs OFF = idle phase
  digitalWrite(RED_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
}

void loop() {
  // ===== IDLE FOR 5 SECONDS =====
  delay(5000);
  // ===== HIGH LOAD FOR 0.5 SECONDS =====
  heavyLoad(500);

  
}