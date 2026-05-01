#include <Arduino.h>
#include "StatusLed.h"
#include "Config.h"
#include "NetworkManager.h"
#include "BleScanner.h"
#include "TvController.h"
#include "Sequence.h"
#include "SetupServer.h"

constexpr int LED_PIN = 8;
constexpr uint32_t COOLDOWN_MS = 60000;

StatusLed led;
Config cfg;
NetworkManager net;
BleScanner scanner;

// Setup-mode owner; allocated only if config missing.
SetupServer* setup_srv = nullptr;

// Runtime-mode state
bool in_runtime_ = false;
uint32_t cooldown_until_ = 0;
volatile bool sequence_pending_ = false;

void enterSetupMode() {
  Serial.println("[boot] entering setup mode");
  led.setState(LedState::Setup);
  setup_srv = new SetupServer(cfg, scanner);
  setup_srv->begin();
}

void enterRuntimeMode() {
  Serial.println("[boot] entering runtime mode");
  net.configure(cfg.tplinkSsid, cfg.tplinkPass, cfg.fritzboxSsid, cfg.fritzboxPass);
  if (!net.connectTplink()) {
    Serial.println("[boot] could not reach TP-Link; staying error");
    led.setState(LedState::Error);
    return;
  }
  led.setState(LedState::Idle);
  in_runtime_ = true;

  scanner.begin();
  scanner.onHit([](const BleHit& h){
    if (millis() < cooldown_until_) return;
    String mac = h.mac; mac.toLowerCase();
    String wanted = cfg.dualsenseMac; wanted.toLowerCase();
    if (mac != wanted) return;
    sequence_pending_ = true;
    cooldown_until_ = millis() + COOLDOWN_MS;
    Serial.printf("[runtime] DualSense detected rssi=%d, queuing sequence\n", h.rssi);
  });
  scanner.start(0);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);

  cfg = Config::load();
  if (!cfg.hasAll()) {
    enterSetupMode();
  } else {
    enterRuntimeMode();
  }
}

void loop() {
  led.tick(millis());

  if (in_runtime_ && sequence_pending_) {
    sequence_pending_ = false;
    scanner.stop();
    Sequence seq({ &cfg, &net, &led });
    seq.run();
    // Restart scanning after sequence ends (network is back on TP-Link).
    scanner.start(0);
  }

  delay(10);
}
