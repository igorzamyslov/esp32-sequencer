#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "StatusLed.h"
#include "Config.h"
#include "NetworkManager.h"
#include "BleScanner.h"
#include "TvController.h"
#include "Sequence.h"
#include "SetupServer.h"

constexpr int LED_PIN = 8;
constexpr uint32_t COOLDOWN_MS = 60000;
constexpr uint32_t WIFI_RETRY_BACKOFF_MS = 15000;

StatusLed led;
Config cfg;
NetworkManager net;
BleScanner scanner;
AsyncWebServer runtime_http(80);

// Setup-mode owner; allocated only if config missing.
SetupServer* setup_srv = nullptr;

// Runtime-mode state
bool in_runtime_ = false;
bool wifi_ready_ = false;
bool runtime_http_started_ = false;
bool ble_started_ = false;
uint32_t next_wifi_retry_ms_ = 0;
uint32_t cooldown_until_ = 0;
volatile bool sequence_pending_ = false;

void enterSetupMode() {
  Serial.println("[boot] entering setup mode");
  led.setState(LedState::Setup);
  setup_srv = new SetupServer(cfg, scanner);
  setup_srv->begin();
}

void startRuntimeHttp() {
  if (runtime_http_started_) return;
  Serial.println("[runtime] registering HTTP routes");
  runtime_http.on("/trigger", HTTP_POST, [](AsyncWebServerRequest* req){
    sequence_pending_ = true;
    cooldown_until_ = millis() + COOLDOWN_MS;
    req->send(200, "text/plain", "queued");
  });
  runtime_http.on("/reset", HTTP_POST, [](AsyncWebServerRequest* req){
    Config::clear();
    req->send(200, "text/plain", "cleared — rebooting");
    delay(500);
    ESP.restart();
  });
  runtime_http.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/plain",
      "esp32-tv runtime\n"
      "POST /trigger  -> run the sequence\n"
      "POST /reset    -> wipe config and reboot to setup mode\n");
  });
  runtime_http.begin();
  runtime_http_started_ = true;
  Serial.printf("[runtime] http on http://%s/\n", net.localIp().c_str());
}

void startBle() {
  if (ble_started_) return;
  Serial.println("[runtime] starting BLE scanner");
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
  ble_started_ = true;
  Serial.println("[runtime] BLE scanner running");
}

void tryConnectIdle() {
  if (millis() < next_wifi_retry_ms_) return;
  if (net.connectFritzbox()) {
    led.setState(LedState::Idle);
    wifi_ready_ = true;
    startRuntimeHttp();
    startBle();
  } else {
    led.setState(LedState::Error);
    Serial.printf("[runtime] idle wifi unreachable, retry in %lus\n",
                  (unsigned long)(WIFI_RETRY_BACKOFF_MS / 1000));
    next_wifi_retry_ms_ = millis() + WIFI_RETRY_BACKOFF_MS;
  }
}

void enterRuntimeMode() {
  Serial.println("[boot] entering runtime mode");
  net.configure(cfg.tplinkSsid, cfg.tplinkPass, cfg.fritzboxSsid, cfg.fritzboxPass);
  net.configureTplinkStatic(cfg.tplinkStaticIp, cfg.tplinkGateway);
  // Idle on Fritzbox: that's where phones live and where /trigger requests come from.
  // The sequence hops to TP-Link briefly to WoL the PC, then comes back.
  // Connect attempt is driven by the loop with backoff so a missing AP doesn't brick boot.
  in_runtime_ = true;
  next_wifi_retry_ms_ = 0; // try immediately on first loop tick
}

void setup() {
  Serial.begin(115200);
  delay(1500); // wait for USB-CDC to enumerate before first println
  Serial.println("[boot] hi");
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

  if (setup_srv) setup_srv->loop();

  if (in_runtime_) {
    if (!wifi_ready_) {
      tryConnectIdle();
    } else if (!net.isConnected()) {
      Serial.println("[runtime] wifi dropped, will reconnect");
      led.setState(LedState::Error);
      wifi_ready_ = false;
      next_wifi_retry_ms_ = millis() + 1000;
    } else if (sequence_pending_) {
      sequence_pending_ = false;
      if (ble_started_) scanner.stop();
      Sequence seq({ &cfg, &net, &led });
      seq.run();
      if (ble_started_) scanner.start(0);
      // After sequence: if we're not back on the idle network, force a reconnect cycle.
      // Sequence::run handles its own LED state on failure; the watchdog above heals WiFi.
      if (!net.isConnected() || net.current() != WifiTarget::Fritzbox) {
        Serial.println("[runtime] post-sequence: not on idle wifi, reconnecting");
        wifi_ready_ = false;
        next_wifi_retry_ms_ = millis() + 1000;
      }
    }
  }

  delay(10);
}
