#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Wol {
    // Parses "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF" (case-insensitive).
    // Returns true on success and fills out[0..5].
    bool parseMac(const char* mac_str, uint8_t out[6]);

    // Writes 102 bytes into out: 6 * 0xFF then mac repeated 16 times.
    void buildMagicPacket(const uint8_t mac[6], uint8_t out[102]);

#ifdef ARDUINO
    // Sends the magic packet as a UDP broadcast on port 9.
    // Caller must be connected to a WiFi network. Returns true if the
    // packet was handed to the stack.
    bool sendBroadcast(const char* mac_str);
#endif
}
