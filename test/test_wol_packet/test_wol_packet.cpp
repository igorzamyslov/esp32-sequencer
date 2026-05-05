#include <unity.h>
#include "Wol.h"
#include <string.h>

void test_parse_mac_colon_separated() {
    uint8_t mac[6];
    TEST_ASSERT_TRUE(Wol::parseMac("AA:BB:CC:11:22:33", mac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, mac[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, mac[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11, mac[3]);
    TEST_ASSERT_EQUAL_HEX8(0x22, mac[4]);
    TEST_ASSERT_EQUAL_HEX8(0x33, mac[5]);
}

void test_parse_mac_dash_separated() {
    uint8_t mac[6];
    TEST_ASSERT_TRUE(Wol::parseMac("aa-bb-cc-11-22-33", mac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0x33, mac[5]);
}

void test_parse_mac_invalid_returns_false() {
    uint8_t mac[6];
    TEST_ASSERT_FALSE(Wol::parseMac("not-a-mac", mac));
    TEST_ASSERT_FALSE(Wol::parseMac("AA:BB:CC:11:22", mac));        // too short
    TEST_ASSERT_FALSE(Wol::parseMac("ZZ:BB:CC:11:22:33", mac));     // bad hex
    TEST_ASSERT_FALSE(Wol::parseMac(nullptr, mac));                 // null
    TEST_ASSERT_FALSE(Wol::parseMac("AA:BB:CC:11:22:33:44", mac));  // too long
    TEST_ASSERT_FALSE(Wol::parseMac("AA:BB-CC:DD-EE:FF", mac));     // mixed separators
    TEST_ASSERT_FALSE(Wol::parseMac("AA BB CC 11 22 33", mac));     // wrong separator
}

void test_magic_packet_is_102_bytes() {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
    uint8_t pkt[102];
    Wol::buildMagicPacket(mac, pkt);
    // First 6 bytes are 0xFF
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, pkt[i]);
    }
    // Next 96 bytes are the MAC repeated 16 times
    for (int rep = 0; rep < 16; rep++) {
        for (int i = 0; i < 6; i++) {
            TEST_ASSERT_EQUAL_HEX8(mac[i], pkt[6 + rep * 6 + i]);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_mac_colon_separated);
    RUN_TEST(test_parse_mac_dash_separated);
    RUN_TEST(test_parse_mac_invalid_returns_false);
    RUN_TEST(test_magic_packet_is_102_bytes);
    return UNITY_END();
}
