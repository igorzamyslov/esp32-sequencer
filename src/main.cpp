#include <Arduino.h>
#include "StatusLed.h"
#include "BleScanner.h"

constexpr int LED_PIN = 8;
StatusLed led;
BleScanner scanner;

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);
  led.setState(LedState::Idle);

  scanner.begin();
  scanner.onHit([](const BleHit& h){
    Serial.printf("[ble] hit mac=%s rssi=%d name='%s'\n",
                  h.mac.c_str(), h.rssi, h.name.c_str());
  });
  scanner.start(0);
}

void loop() { led.tick(millis()); delay(10); }
