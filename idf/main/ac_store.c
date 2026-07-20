#include "ac_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ac_store";
static const char *NS = "acstore";
static const char *KEY = "gwstate";

bool ac_store_load(gw_state_t *out) {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  size_t len = sizeof(gw_state_t);
  esp_err_t err = nvs_get_blob(h, KEY, out, &len);
  nvs_close(h);
  if (err == ESP_OK && len == sizeof(gw_state_t)) {
    ESP_LOGI(TAG, "Loaded saved A/C state");
    return true;
  }
  return false;
}

void ac_store_save(const gw_state_t *state) {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open failed");
    return;
  }
  esp_err_t err = nvs_set_blob(h, KEY, state, sizeof(gw_state_t));
  if (err == ESP_OK) {
    nvs_commit(h);
  } else {
    ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
  }
  nvs_close(h);
}
