#include <Arduino.h>
#include "StatusLed.h"

constexpr int LED_PIN = 8;
StatusLed led;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[boot] led demo");
  led.attachPin(LED_PIN);
  led.setState(LedState::Idle);
}

void loop() {
  led.tick(millis());
  delay(10);
}
