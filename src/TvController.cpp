#include "TvController.h"
#include <ArduinoJson.h>
#include <base64.h>

TvController* TvController::s_instance_ = nullptr;

void TvController::configure(const String& ip, const String& token, TokenCallback onToken) {
    ip_ = ip;
    token_ = token;
    onToken_ = onToken;
}

String TvController::buildPath() const {
    // name must be base64-encoded
    String name = base64::encode("esp32-tv");
    String path = "/api/v2/channels/samsung.remote.control?name=" + name;
    if (token_.length()) path += "&token=" + token_;
    return path;
}

bool TvController::connectWithRetry(uint32_t total_timeout_ms) {
    s_instance_ = this;
    ws_.beginSslWithCA(ip_.c_str(), 8002, buildPath().c_str(), nullptr, "");
    ws_.onEvent(&TvController::onEventStatic);
    ws_.setReconnectInterval(1000);

    uint32_t start = millis();
    while (millis() - start < total_timeout_ms) {
        ws_.loop();
        if (connected_) return true;
        delay(50);
    }
    return false;
}

bool TvController::sendKey(const char* key) {
    if (!connected_) return false;
    JsonDocument doc;
    doc["method"] = "ms.remote.control";
    auto params = doc["params"].to<JsonObject>();
    params["Cmd"] = "Click";
    params["DataOfCmd"] = key;
    params["Option"] = "false";
    params["TypeOfRemote"] = "SendRemoteKey";
    String out;
    serializeJson(doc, out);
    return ws_.sendTXT(out);
}

void TvController::disconnect() {
    ws_.disconnect();
    connected_ = false;
}

void TvController::onEventStatic(WStype_t type, uint8_t* payload, size_t len) {
    if (s_instance_) s_instance_->onEvent(type, payload, len);
}

void TvController::onEvent(WStype_t type, uint8_t* payload, size_t len) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.printf("[tv] connected path=%s\n", buildPath().c_str());
            break;
        case WStype_DISCONNECTED:
            Serial.println("[tv] disconnected");
            connected_ = false;
            break;
        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, len);
            if (err) {
                Serial.printf("[tv] bad json: %s\n", err.c_str());
                return;
            }
            const char* event = doc["event"] | "";
            Serial.printf("[tv] event=%s\n", event);
            if (strcmp(event, "ms.channel.connect") == 0) {
                connected_ = true;
                const char* tok = doc["data"]["token"] | "";
                if (tok && *tok && onToken_) {
                    String t(tok);
                    if (t != token_) {
                        token_ = t;
                        onToken_(t);
                    }
                }
            } else if (strcmp(event, "ms.channel.unauthorized") == 0) {
                Serial.println("[tv] unauthorized — accept the prompt on the TV remote");
            }
            break;
        }
        default: break;
    }
}
