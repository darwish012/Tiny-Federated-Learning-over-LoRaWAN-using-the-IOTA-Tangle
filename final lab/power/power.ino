/*
 * Arduino Uno Serial Power Profiler
 * Setup: A0 connected to 1 Ohm shunt resistor
 * Reference: Standard 5V
 */

const int analogPin = A0;
const float shuntResistor = 1.0;     // Value in Ohms
const float referenceVoltage = 5.0;  // Standard 5V Reference

// Moving average filter to smooth out fast radio bursts
// Set numReadings > 1 to enable averaging; 1 means no smoothing.
const int numReadings = 1; 
int readings[numReadings];
int readIndex = 0;
long total = 0;

void setup() {
  Serial.begin(115200);
  
  // No analogReference() call needed; this sketch uses the default 5V ADC reference.
  
  // Initialize the smoothing array
  for (int i = 0; i < numReadings; i++) readings[i] = 0;
  
  Serial.println("mA_Output"); 
}

void loop() {
  // --- Smoothing Filter ---
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(analogPin);
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % numReadings;

  float averageADC = (float)total / numReadings;

  // --- Calculation ---
  // Convert ADC back to actual voltage (0 to 5V)
  float voltageAtA0 = (averageADC / 1023.0) * referenceVoltage;
  
  // Apply Ohm's Law (I = V / R)
  float currentAmps = voltageAtA0 / shuntResistor;
  float currentmA = currentAmps * 1000.0;

  // Output for the Python Data Logger
  Serial.println(currentmA);

  // High-speed sampling to catch the radio
  delay(10); 
}