#include <unity.h>
#include "StatusLed.h"

void test_idle_heartbeat_brief_blip_every_3s() {
    StatusLed led;
    led.setState(LedState::Idle);
    // At t=0, blip starts
    TEST_ASSERT_TRUE(led.isOnAt(0));
    // 100ms blip width
    TEST_ASSERT_TRUE(led.isOnAt(99));
    TEST_ASSERT_FALSE(led.isOnAt(101));
    // Off for the rest of the 3s period
    TEST_ASSERT_FALSE(led.isOnAt(2999));
    // Next blip
    TEST_ASSERT_TRUE(led.isOnAt(3000));
}

void test_running_fast_blink_5hz() {
    StatusLed led;
    led.setState(LedState::Running);
    // Period 200ms, on for first 100ms, off for second 100ms
    TEST_ASSERT_TRUE(led.isOnAt(0));
    TEST_ASSERT_TRUE(led.isOnAt(99));
    TEST_ASSERT_FALSE(led.isOnAt(100));
    TEST_ASSERT_FALSE(led.isOnAt(199));
    TEST_ASSERT_TRUE(led.isOnAt(200));
}

void test_success_solid_for_2s_then_idle() {
    StatusLed led;
    led.setState(LedState::Success);
    // Anchor the timeline at t=0 with an initial tick.
    led.tick(0);
    TEST_ASSERT_TRUE(led.isOnAt(0));
    TEST_ASSERT_TRUE(led.isOnAt(1999));
    // After 2000ms, transitions to Idle automatically
    led.tick(2001);
    TEST_ASSERT_EQUAL(LedState::Idle, led.state());
}

void test_setup_slow_1hz_blink() {
    StatusLed led;
    led.setState(LedState::Setup);
    // 500ms on, 500ms off
    TEST_ASSERT_TRUE(led.isOnAt(0));
    TEST_ASSERT_TRUE(led.isOnAt(499));
    TEST_ASSERT_FALSE(led.isOnAt(500));
    TEST_ASSERT_FALSE(led.isOnAt(999));
    TEST_ASSERT_TRUE(led.isOnAt(1000));
}

void test_error_three_pulses_then_idle() {
    StatusLed led;
    led.setState(LedState::Error);
    // Anchor the timeline at t=0 with an initial tick.
    led.tick(0);
    // Three 100ms pulses with 100ms gaps: on 0-100, off 100-200, on 200-300, off 300-400, on 400-500, off 500-600
    TEST_ASSERT_TRUE(led.isOnAt(50));
    TEST_ASSERT_FALSE(led.isOnAt(150));
    TEST_ASSERT_TRUE(led.isOnAt(250));
    TEST_ASSERT_FALSE(led.isOnAt(350));
    TEST_ASSERT_TRUE(led.isOnAt(450));
    TEST_ASSERT_FALSE(led.isOnAt(550));
    led.tick(601);
    TEST_ASSERT_EQUAL(LedState::Idle, led.state());
}

void test_tick_anchors_at_first_call_not_at_zero() {
    StatusLed led;
    led.setState(LedState::Success);
    // First tick at t=50000ms: this is when the timeline actually begins.
    led.tick(50000);
    TEST_ASSERT_EQUAL(LedState::Success, led.state()); // must NOT have auto-flipped
    TEST_ASSERT_TRUE(led.currentlyOn());
    // Still within 2s of state entry (50000): solid on
    led.tick(51999);
    TEST_ASSERT_EQUAL(LedState::Success, led.state());
    // 2001ms after first tick: must auto-transition to Idle
    led.tick(52001);
    TEST_ASSERT_EQUAL(LedState::Idle, led.state());
}

void test_tick_error_anchors_at_first_call_not_at_zero() {
    StatusLed led;
    led.setState(LedState::Error);
    led.tick(50000);
    TEST_ASSERT_EQUAL(LedState::Error, led.state()); // must NOT have auto-flipped
    led.tick(50601);
    TEST_ASSERT_EQUAL(LedState::Idle, led.state());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_heartbeat_brief_blip_every_3s);
    RUN_TEST(test_running_fast_blink_5hz);
    RUN_TEST(test_success_solid_for_2s_then_idle);
    RUN_TEST(test_setup_slow_1hz_blink);
    RUN_TEST(test_error_three_pulses_then_idle);
    RUN_TEST(test_tick_anchors_at_first_call_not_at_zero);
    RUN_TEST(test_tick_error_anchors_at_first_call_not_at_zero);
    return UNITY_END();
}
