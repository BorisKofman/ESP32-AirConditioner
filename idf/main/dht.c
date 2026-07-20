#include "dht.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"      // esp_rom_delay_us
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

static const char *TAG = "dht";
static int s_gpio = -1;

void dht_init(int gpio_num) {
  s_gpio = gpio_num;
  gpio_set_direction((gpio_num_t)s_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_level((gpio_num_t)s_gpio, 1);  // idle high (open-drain + pull-up)
  ESP_LOGI(TAG, "DHT22 initialized on GPIO %d", s_gpio);
}

// Wait until the line reaches `level`, up to timeout_us. Returns elapsed us,
// or -1 on timeout.
static int wait_level(int level, int timeout_us) {
  int us = 0;
  while (gpio_get_level((gpio_num_t)s_gpio) != level) {
    if (us++ > timeout_us) return -1;
    esp_rom_delay_us(1);
  }
  return us;
}

bool dht_read(float *temp_c, float *humidity) {
  if (s_gpio < 0) return false;
  uint8_t data[5] = {0};

  // Start signal: pull low >=1ms, then release and let the sensor respond.
  // Timing is tight, so guard the sampling section from task preemption.
  gpio_set_level((gpio_num_t)s_gpio, 0);
  esp_rom_delay_us(1200);           // >=1ms low
  gpio_set_level((gpio_num_t)s_gpio, 1);
  esp_rom_delay_us(30);

  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  taskENTER_CRITICAL(&mux);

  bool ok = true;
  // Sensor pulls low ~80us, then high ~80us as its response.
  if (wait_level(0, 100) < 0) ok = false;
  if (ok && wait_level(1, 100) < 0) ok = false;
  if (ok && wait_level(0, 100) < 0) ok = false;

  // 40 bits: each starts with ~50us low, then a high whose length encodes the
  // bit (~26-28us = 0, ~70us = 1).
  for (int i = 0; ok && i < 40; i++) {
    if (wait_level(1, 100) < 0) { ok = false; break; }  // end of the 50us low
    int high_us = wait_level(0, 150);                   // measure high length
    if (high_us < 0) { ok = false; break; }
    data[i / 8] <<= 1;
    if (high_us > 45) data[i / 8] |= 1;                 // long high => bit 1
  }

  taskEXIT_CRITICAL(&mux);
  gpio_set_level((gpio_num_t)s_gpio, 1);  // release line

  if (!ok) {
    ESP_LOGW(TAG, "DHT read timeout");
    return false;
  }
  // Checksum: sum of first 4 bytes (low 8 bits) must equal byte 5.
  if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4]) {
    ESP_LOGW(TAG, "DHT checksum error");
    return false;
  }

  // DHT22: humidity = 16-bit /10 (%RH); temperature = 16-bit /10 (C),
  // top bit of the high byte is the sign.
  uint16_t raw_h = (data[0] << 8) | data[1];
  uint16_t raw_t = (data[2] << 8) | data[3];
  *humidity = raw_h / 10.0f;
  float t = (raw_t & 0x7FFF) / 10.0f;
  if (raw_t & 0x8000) t = -t;
  *temp_c = t;
  return true;
}
