#include "Wol.h"
#include <string.h>
#include <ctype.h>

#ifdef ARDUINO
#include <WiFi.h>
#include <WiFiUdp.h>
#endif

namespace {
    int hexNibble(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        c = (char)tolower((unsigned char)c);
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        return -1;
    }
}

namespace Wol {

bool parseMac(const char* mac_str, uint8_t out[6]) {
    if (!mac_str) return false;
    // Expect 17 chars: 6 hex pairs separated by ':' or '-'
    if (strlen(mac_str) != 17) return false;
    char sep = mac_str[2];
    if (sep != ':' && sep != '-') return false;
    for (int i = 0; i < 6; i++) {
        int hi = hexNibble(mac_str[i * 3]);
        int lo = hexNibble(mac_str[i * 3 + 1]);
        if (hi < 0 || lo < 0) return false;
        if (i < 5 && mac_str[i * 3 + 2] != sep) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void buildMagicPacket(const uint8_t mac[6], uint8_t out[102]) {
    for (int i = 0; i < 6; i++) out[i] = 0xFF;
    for (int rep = 0; rep < 16; rep++) {
        for (int i = 0; i < 6; i++) {
            out[6 + rep * 6 + i] = mac[i];
        }
    }
}

#ifdef ARDUINO
bool sendBroadcast(const char* mac_str) {
    uint8_t mac[6];
    if (!parseMac(mac_str, mac)) return false;
    uint8_t pkt[102];
    buildMagicPacket(mac, pkt);

    WiFiUDP udp;
    if (!udp.beginPacket(IPAddress(255, 255, 255, 255), 9)) return false;
    udp.write(pkt, sizeof(pkt));
    return udp.endPacket() == 1;
}
#endif

}
