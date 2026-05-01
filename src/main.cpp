#include <Arduino.h>
#include "StatusLed.h"
#include "Config.h"

constexpr int LED_PIN = 8;
StatusLed led;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[boot] config demo");
  led.attachPin(LED_PIN);

  Config c = Config::load();
  Serial.printf("[cfg] hasAll=%d  tplinkSsid='%s'  pcMac='%s'  tvIp='%s'\n",
                c.hasAll(), c.tplinkSsid.c_str(), c.pcMac.c_str(), c.tvIp.c_str());

  led.setState(c.hasAll() ? LedState::Idle : LedState::Setup);
}

void loop() {
  led.tick(millis());
  delay(10);
}
