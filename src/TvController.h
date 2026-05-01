#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <functional>

class TvController {
public:
    // ip: TV's IP. token: empty for first pairing, otherwise saved token.
    // onTokenIssued: called when the TV provides a fresh token (during pairing).
    using TokenCallback = std::function<void(const String&)>;

    void configure(const String& ip, const String& token, TokenCallback onToken);

    // Connect with retries up to total_timeout_ms; each attempt has its own short timeout.
    bool connectWithRetry(uint32_t total_timeout_ms = 15000);

    // Send a single Tizen remote-control key. Returns true if the websocket
    // was open and the message was queued.
    bool sendKey(const char* key);

    void disconnect();

private:
    WebSocketsClient ws_;
    String ip_;
    String token_;
    TokenCallback onToken_;
    bool connected_ = false;

    void onEvent(WStype_t type, uint8_t* payload, size_t len);
    static void onEventStatic(WStype_t type, uint8_t* payload, size_t len);
    static TvController* s_instance_;

    String buildPath() const;
};
