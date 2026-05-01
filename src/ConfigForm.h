#pragma once
#include <Arduino.h>

class AsyncWebServerRequest;
struct Config;

namespace ConfigForm {
    // Render the settings form HTML, pre-filled with current cfg values.
    // include_pair=true adds the "Pair gamepad" section (used in setup mode only).
    String renderHtml(const Config& cfg, bool include_pair);

    // Apply form fields to cfg and persist via cfg.save(). Empty password fields
    // leave the existing stored password untouched.
    void applySave(AsyncWebServerRequest* req, Config& cfg);
}
