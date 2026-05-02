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
static bool sendOne(const uint8_t pkt[102], IPAddress dst, uint16_t port) {
    WiFiUDP udp;
    if (!udp.beginPacket(dst, port)) return false;
    udp.write(pkt, 102);
    return udp.endPacket() == 1;
}

bool sendBroadcast(const char* mac_str) {
    uint8_t mac[6];
    if (!parseMac(mac_str, mac)) return false;
    uint8_t pkt[102];
    buildMagicPacket(mac, pkt);

    // Samsung TVs in network-standby poll WiFi infrequently (DTIM interval),
    // so a single magic packet often gets missed. Send to both global and
    // local subnet broadcast, repeated, on the canonical WoL ports (7 and 9).
    IPAddress global(255, 255, 255, 255);
    IPAddress ip = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    IPAddress subnet(
        ip[0] | (uint8_t)~mask[0],
        ip[1] | (uint8_t)~mask[1],
        ip[2] | (uint8_t)~mask[2],
        ip[3] | (uint8_t)~mask[3]);

    bool any_ok = false;
    for (int rep = 0; rep < 5; rep++) {
        if (sendOne(pkt, global, 9)) any_ok = true;
        if (sendOne(pkt, global, 7)) any_ok = true;
        if (sendOne(pkt, subnet, 9)) any_ok = true;
        if (sendOne(pkt, subnet, 7)) any_ok = true;
        delay(150);
    }
    return any_ok;
}
#endif

}
