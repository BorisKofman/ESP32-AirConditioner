// Native ESP-IDF Goodweather A/C IR encoder (RMT-based), ported from
// IRremoteESP8266's ir_Goodweather.cpp. Replaces the Arduino IRac/IRsend path
// so the firmware can build against pure esp-matter (Matter-over-Thread) with
// no Arduino core dependency.
//
// Protocol (from ir_Goodweather.{h,cpp}):
//   - 38 kHz carrier
//   - Header: mark 6820us, space 6820us
//   - 6 data bytes, each sent as 16 bits: byte LSB-first, then its inverted
//     copy (~byte). The AC uses the inverted copy as an integrity check.
//   - Bit: mark 580us; space 580us => '1', space 1860us => '0'
//   - Footer: mark 580us, space 6820us, mark 580us, long gap
//   - 48-bit state in a uint64_t; init value 0xD50000000000
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Native mode/fan/swing values (match the Goodweather constants).
typedef enum { GW_AUTO = 0, GW_COOL = 1, GW_DRY = 2, GW_FAN = 3, GW_HEAT = 4 } gw_mode_t;
typedef enum { GW_FAN_AUTO = 0, GW_FAN_HIGH = 1, GW_FAN_MED = 2, GW_FAN_LOW = 3 } gw_fan_t;
typedef enum { GW_SWING_FAST = 0, GW_SWING_SLOW = 1, GW_SWING_OFF = 2 } gw_swing_t;

// Full desired A/C state; the encoder builds the 48-bit frame from this.
typedef struct {
  bool power;
  gw_mode_t mode;
  uint8_t temp_c;     // 16..31
  gw_fan_t fan;
  gw_swing_t swing;
  bool light;
  bool turbo;
  bool sleep;
  uint8_t command;    // which button/field this frame represents (kGoodweatherCmd*)
} gw_state_t;

// Initialize the RMT TX channel on the given GPIO. Call once at startup.
void gw_ir_init(int gpio_num);

// Build the 48-bit Goodweather frame from state and transmit it over RMT.
void gw_ir_send(const gw_state_t *state);

// Expose the raw frame builder for unit-style checks (returns 48-bit value).
uint64_t gw_ir_build_raw(const gw_state_t *state);

#ifdef __cplusplus
}
#endif
