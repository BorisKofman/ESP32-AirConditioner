#include "dht.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dht";
static int s_gpio = -1;

// Edge-timestamp capture. The sensor's reply is a train of high pulses whose
// LENGTH encodes the data (~26-28us = 0, ~70us = 1, and one ~80us response
// pulse up front). The ISR stamps rising edges and appends each completed
// high-pulse duration; the reader task just sleeps while the ~5 ms transfer
// happens. This replaces the old busy-poll driver, which sampled inside
// taskENTER_CRITICAL — ~5 ms with ALL interrupts masked (long enough to drop
// 802.15.4 frames on this C6) — and whose "microseconds" were really loop
// iterations, calibrated to the CPU frequency by luck.
//
// Expected pulses: 1 response + 40 bits = 41; our own release-to-sensor-pull
// handover can add one spurious ~30us high in front, so parse the LAST 40.
#define DHT_MAX_HIGHS 48
static volatile int64_t  s_rise_us;
static volatile uint16_t s_high_us[DHT_MAX_HIGHS];
static volatile int      s_high_count;

static void IRAM_ATTR dht_isr(void *arg) {
  int64_t now = esp_timer_get_time();
  if (gpio_get_level((gpio_num_t)s_gpio)) {
    s_rise_us = now;
  } else if (s_rise_us > 0) {
    int64_t d = now - s_rise_us;
    s_rise_us = 0;
    int i = s_high_count;
    if (i < DHT_MAX_HIGHS && d < UINT16_MAX) {
      s_high_us[i] = (uint16_t)d;
      s_high_count = i + 1;
    }
  }
}

void dht_init(int gpio_num) {
  s_gpio = gpio_num;
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << s_gpio,
      .mode = GPIO_MODE_INPUT_OUTPUT_OD,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)s_gpio, 1);  // idle high (open-drain + pull-up)

  // Shared GPIO ISR service; another component may have installed it already.
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
  }
  ESP_LOGI(TAG, "DHT22 initialized on GPIO %d", s_gpio);
}

bool dht_read(float *temp_c, float *humidity) {
  if (s_gpio < 0) return false;

  // Start signal: hold the line low >=1 ms. vTaskDelay (not a busy-wait):
  // nothing time-critical happens until the sensor starts answering.
  gpio_set_level((gpio_num_t)s_gpio, 0);
  vTaskDelay(pdMS_TO_TICKS(2));

  // Arm the edge capture BEFORE releasing the line so no edge is missed,
  // then release and sleep through the ~5 ms transfer.
  s_rise_us = 0;
  s_high_count = 0;
  gpio_isr_handler_add((gpio_num_t)s_gpio, dht_isr, NULL);
  gpio_set_intr_type((gpio_num_t)s_gpio, GPIO_INTR_ANYEDGE);
  gpio_intr_enable((gpio_num_t)s_gpio);
  gpio_set_level((gpio_num_t)s_gpio, 1);

  vTaskDelay(pdMS_TO_TICKS(8));

  gpio_intr_disable((gpio_num_t)s_gpio);
  gpio_isr_handler_remove((gpio_num_t)s_gpio);
  gpio_set_level((gpio_num_t)s_gpio, 1);  // leave the line released

  int n = s_high_count;
  if (n < 41) {
    ESP_LOGW(TAG, "DHT read failed: %d pulses (want >=41)", n);
    return false;
  }

  // The last 40 high pulses are the data bits, MSB-first.
  uint8_t data[5] = {0};
  for (int i = 0; i < 40; i++) {
    data[i / 8] <<= 1;
    if (s_high_us[n - 40 + i] > 48) data[i / 8] |= 1;  // ~27us = 0, ~70us = 1
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
