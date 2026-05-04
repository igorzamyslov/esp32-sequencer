#pragma once
#include <Arduino.h>

class AsyncWebServerRequest;
struct Config;

namespace ConfigForm {
    // Render the bootstrap-only settings form (WiFi credentials).
    String renderHtml(const Config& cfg, bool include_pair_unused = false);

    // Apply form fields to cfg and persist via cfg.save(). Empty password fields
    // leave the existing stored password untouched.
    void applySave(AsyncWebServerRequest* req, Config& cfg);
}
