// Minimal native DHT22 (AM2302) driver — bit-bang, no Arduino dependency.
// Replaces the Arduino DHT library from the WiFi build.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configure the DHT data GPIO. Call once at startup.
void dht_init(int gpio_num);

// Read the sensor. On success, fills *temp_c and *humidity (percent) and
// returns true. Returns false on timeout/checksum error (caller keeps last
// good value). DHT22 must not be polled faster than ~once every 2s.
bool dht_read(float *temp_c, float *humidity);

#ifdef __cplusplus
}
#endif
