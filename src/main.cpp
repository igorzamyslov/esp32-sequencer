#include <Arduino.h>
#include "StatusLed.h"

constexpr int LED_PIN = 8;
StatusLed led;

void setup() { Serial.begin(115200); led.attachPin(LED_PIN); led.setState(LedState::Idle); }
void loop()  { led.tick(millis()); delay(10); }
