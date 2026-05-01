#include <Arduino.h>

constexpr int LED_PIN = 8;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[boot] esp32-tv smoke test");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // off (active LOW)
}

void loop() {
  digitalWrite(LED_PIN, LOW);
  delay(500);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  Serial.println("[loop] tick");
}
