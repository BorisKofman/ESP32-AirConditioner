// Native (Arduino-free) Matter-over-Thread A/C controller for ESP32-C6.
// Ports the thermostat/fan/humidity logic from the Arduino ThermostatAccessory
// onto esp-matter's C++ API, and drives the Goodweather A/C via the native RMT
// IR encoder (goodweather_ir.c). No Arduino core, no IRremoteESP8266.
//
// Endpoints:
//   - Thermostat (heat/cool setpoints + system mode) -> Goodweather IR
//   - Fan (speed %)                                  -> Goodweather fan field
//   - Relative Humidity sensor                       <- DHT22
// Local temperature is fed from the DHT22 as well.

#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_endpoint.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
// Matter-over-Thread: the OpenThread platform config must be set before start().
#include <esp_openthread_types.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <app/server/Server.h>  // re-open commissioning window on kFabricRemoved

#include "goodweather_ir.h"
#include "ac_store.h"
#include "dht.h"
#include "custom_commissioning.h"
#include <setup_payload/OnboardingCodesUtil.h>  // PrintOnboardingCodes

// ---- Config (mirrors the old Config.h; adjust pins for your C6 wiring) ----
#define IR_SEND_GPIO       4
#define DHT_GPIO           5
#define AC_MIN_TEMP_C      16
#define AC_MAX_TEMP_C      31
#define AC_SEND_DEBOUNCE_MS 500
#define DHT_READ_MS        30000   // DHT22 poll interval
#define TEMP_OFFSET_C      2.0     // subtract self-heating (from old Config.h)
// Factory-reset button: hold the BOOT button (GPIO9) this long to decommission.
#define RESET_BUTTON_GPIO  9
#define RESET_HOLD_MS      5000

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "ac_matter";

// One-line, thread-safe attribute update. attribute::update() must run with the
// Matter stack lock held; we call it from the main-loop task (not the Matter
// thread), so take the scoped lock here. RAII releases it on scope exit.
static inline void set_attr(uint16_t ep, uint32_t cluster, uint32_t attr,
                            esp_matter_attr_val_t v) {
  esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
  attribute::update(ep, cluster, attr, &v);
}

// Cluster/attribute IDs we touch.
static constexpr uint32_t kThermostatCluster = Thermostat::Id;
static constexpr uint32_t kFanCluster        = FanControl::Id;
static constexpr uint32_t kTempCluster       = TemperatureMeasurement::Id;
static constexpr uint32_t kHumidityCluster   = RelativeHumidityMeasurement::Id;

// Thermostat SystemMode enum values (Matter spec): 0=Off,3=Cool,4=Heat,1=Auto.
// AutoMode is not enabled (see create_endpoints), so only these are used.
enum { MODE_OFF = 0, MODE_COOL = 3, MODE_HEAT = 4 };

// ---- Shadow state (equivalent to ThermostatAccessory's shadows) ----
static uint16_t s_thermostat_ep = 0;
static uint16_t s_fan_ep = 0;
static uint16_t s_temp_ep = 0;
static uint16_t s_humidity_ep = 0;

static gw_state_t s_ac;                 // current desired A/C state
static uint8_t s_system_mode = MODE_COOL;
static double s_cool_setpoint = 22.0;
static double s_heat_setpoint = 19.5;
static int s_pending_fan_percent = -1;
static int s_last_fan_percent = -1;     // last applied fan %, for change detection
static TickType_t s_apply_due = 0;      // debounce deadline; 0 = not scheduled

// ---- IR emission: build gw_state from shadows and send ----
static void apply_to_ac() {
  s_apply_due = 0;

  if (s_system_mode == MODE_OFF) {
    s_ac.power = false;
    s_ac.command = 0x00;  // kGoodweatherCmdPower
    ESP_LOGI(TAG, "AC -> OFF");
    gw_ir_send(&s_ac);
    ac_store_save(&s_ac);

    // Thermostat off => the AC's fan is off too. Reflect that on the fan
    // endpoint so Apple Home doesn't keep showing a running fan. Setting
    // PercentSetting to 0 and FanMode to Off; guard against our own write
    // re-queuing an IR send via the change-detection state.
    s_last_fan_percent = 0;
    set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentSetting::Id,
             esp_matter_nullable_uint8(0));
    set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::FanMode::Id,
             esp_matter_enum8(0));  // FanModeEnum::kOff
    return;
  }

  s_ac.power = true;
  double target = (s_system_mode == MODE_HEAT) ? s_heat_setpoint : s_cool_setpoint;
  if (target < AC_MIN_TEMP_C) target = AC_MIN_TEMP_C;
  if (target > AC_MAX_TEMP_C) target = AC_MAX_TEMP_C;
  s_ac.temp_c = (uint8_t)(target + 0.5);

  switch (s_system_mode) {
    case MODE_HEAT: s_ac.mode = GW_HEAT; s_ac.command = 0x01; break;
    case MODE_COOL: s_ac.mode = GW_COOL; s_ac.command = 0x01; break;
    default:        s_ac.mode = GW_COOL; s_ac.command = 0x01; break;
  }
  ESP_LOGI(TAG, "AC -> mode %d, target %dC", s_ac.mode, s_ac.temp_c);
  gw_ir_send(&s_ac);
  ac_store_save(&s_ac);
}

static void schedule_apply() {
  s_apply_due = xTaskGetTickCount() + pdMS_TO_TICKS(AC_SEND_DEBOUNCE_MS);
  if (s_apply_due == 0) s_apply_due = 1;
}

// ---- Attribute write callback: controller changed something ----
static esp_err_t attribute_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                              uint32_t cluster_id, uint32_t attribute_id,
                              esp_matter_attr_val_t *val, void *priv) {
  if (type != attribute::POST_UPDATE) {
    return ESP_OK;
  }

  if (endpoint_id == s_thermostat_ep && cluster_id == kThermostatCluster) {
    switch (attribute_id) {
      case Thermostat::Attributes::SystemMode::Id: {
        // Only act on a REAL change. POST_UPDATE also fires when the app
        // re-writes the same value on reconnect/sync; without this guard that
        // re-triggers the AC with identical settings every time you reopen Home.
        // MUST NOT call apply_to_ac() here (Matter event-loop thread; gw_ir_send
        // blocks) - defer to the main loop via schedule_apply().
        uint8_t m = val->val.u8;
        if (m != s_system_mode) {
          s_system_mode = m;
          ESP_LOGI(TAG, "SystemMode -> %d", s_system_mode);
          schedule_apply();
        }
        break;
      }
      case Thermostat::Attributes::OccupiedCoolingSetpoint::Id: {
        double v = val->val.i16 / 100.0;
        if (v != s_cool_setpoint) {
          s_cool_setpoint = v;
          ESP_LOGI(TAG, "Cool setpoint -> %.1f", s_cool_setpoint);
          schedule_apply();
        }
        break;
      }
      case Thermostat::Attributes::OccupiedHeatingSetpoint::Id: {
        double v = val->val.i16 / 100.0;
        if (v != s_heat_setpoint) {
          s_heat_setpoint = v;
          ESP_LOGI(TAG, "Heat setpoint -> %.1f", s_heat_setpoint);
          schedule_apply();
        }
        break;
      }
      default: break;
    }
  } else if (endpoint_id == s_fan_ep && cluster_id == kFanCluster) {
    if (attribute_id == FanControl::Attributes::PercentSetting::Id) {
      // PercentSetting is a nullable uint8; null (0xFF sentinel) means "off".
      bool is_null = (val->type == ESP_MATTER_VAL_TYPE_NULLABLE_UINT8 && val->val.u8 == 0xFF);
      int pct = is_null ? 0 : val->val.u8;
      // The AC only has 3 fan speeds, so snap any value to a discrete step
      // (0 / 33 / 66 / 99) and reflect the snapped value in the app.
      int snapped = (pct == 0)   ? 0
                    : (pct <= 33) ? 33
                    : (pct <= 66) ? 66
                                  : 99;
      if (snapped != s_last_fan_percent) {
        s_last_fan_percent = snapped;
        s_pending_fan_percent = snapped;
        ESP_LOGI(TAG, "Fan percent %d -> snapped %d", pct, snapped);
      }
      // Write the snapped value back so the app shows 33/66/99, not e.g. 80.
      if (snapped != pct) {
        set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentSetting::Id,
                 esp_matter_nullable_uint8((uint8_t)snapped));
        set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentCurrent::Id,
                 esp_matter_uint8((uint8_t)snapped));  // PercentCurrent is non-nullable
      }
    }
  }
  return ESP_OK;
}

// ---- Map fan percent to Goodweather fan field, then send ----
static void apply_fan(int percent) {
  if (percent <= 0)       s_ac.fan = GW_FAN_AUTO;
  else if (percent <= 33) s_ac.fan = GW_FAN_LOW;
  else if (percent <= 66) s_ac.fan = GW_FAN_MED;
  else                    s_ac.fan = GW_FAN_HIGH;
  s_ac.command = 0x05;  // kGoodweatherCmdFan
  gw_ir_send(&s_ac);
  ac_store_save(&s_ac);
}

// ---- Matter stack event callback ----
static void event_cb(const ChipDeviceEvent *event, intptr_t arg) {
  switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
      ESP_LOGI(TAG, "Commissioning complete");
      break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
      ESP_LOGW(TAG, "Commissioning failed (fail-safe expired)");
      break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
      // Last fabric gone (e.g. removed from Apple Home) -> re-open the
      // commissioning window so the device is pairable again WITHOUT a manual
      // factory reset. Matches esp-matter's light example behavior.
      ESP_LOGI(TAG, "Fabric removed");
      if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
        constexpr auto kTimeout = chip::System::Clock::Seconds16(300);
        auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
        if (!mgr.IsCommissioningWindowOpen()) {
          mgr.OpenBasicCommissioningWindow(
              kTimeout, chip::CommissioningWindowAdvertisement::kDnssdOnly);
        }
      }
      break;
    }
    default:
      break;
  }
}

// ---- Identify cluster callback (required 3rd arg to node::create) ----
static esp_err_t identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                   uint8_t effect_id, uint8_t effect_variant, void *priv_data) {
  ESP_LOGI(TAG, "Identify: endpoint %d, effect %d", endpoint_id, effect_id);
  return ESP_OK;
}

static void create_endpoints(node_t *node) {
  // Thermostat: the base cluster's create() asserts that the Heating/Cooling
  // feature bits are already declared in feature_flags, so set them BEFORE
  // create(). Setpoints then come from the feature configs added afterward.
  endpoint::thermostat::config_t th_cfg;
  th_cfg.thermostat.control_sequence_of_operation = 4;  // cooling & heating
  th_cfg.thermostat.system_mode = s_system_mode;
  // Cooling + Heating only. AutoMode is intentionally NOT enabled: it forces a
  // deadband coupling (cool >= heat + deadband) inside the CHIP thermostat
  // cluster, so raising heat pushes cool up and clobbers the user's cool value.
  // Without AutoMode, cool and heat are fully independent. Modes: Off/Cool/Heat.
  th_cfg.thermostat.feature_flags = cluster::thermostat::feature::cooling::get_id() |
                                    cluster::thermostat::feature::heating::get_id();
  endpoint_t *th = endpoint::thermostat::create(node, &th_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_thermostat_ep = endpoint::get_id(th);

  cluster_t *th_cluster = cluster::get(th, kThermostatCluster);
  cluster::thermostat::feature::cooling::config_t cool_feat;
  cool_feat.occupied_cooling_setpoint = (int16_t)(s_cool_setpoint * 100);
  cluster::thermostat::feature::cooling::add(th_cluster, &cool_feat);
  cluster::thermostat::feature::heating::config_t heat_feat;
  heat_feat.occupied_heating_setpoint = (int16_t)(s_heat_setpoint * 100);
  cluster::thermostat::feature::heating::add(th_cluster, &heat_feat);

  // Fan
  endpoint::fan::config_t fan_cfg;
  endpoint_t *fan = endpoint::fan::create(node, &fan_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_fan_ep = endpoint::get_id(fan);

  // Temperature (local temp source)
  endpoint::temperature_sensor::config_t temp_cfg;
  endpoint_t *temp = endpoint::temperature_sensor::create(node, &temp_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_temp_ep = endpoint::get_id(temp);

  // Humidity
  endpoint::humidity_sensor::config_t hum_cfg;
  endpoint_t *hum = endpoint::humidity_sensor::create(node, &hum_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_humidity_ep = endpoint::get_id(hum);

  ESP_LOGI(TAG, "Endpoints: thermostat=%d fan=%d temp=%d humidity=%d",
           s_thermostat_ep, s_fan_ep, s_temp_ep, s_humidity_ep);
}

extern "C" void app_main(void) {
  // Silence esp-matter's per-attribute-update dump ("Endpoint ... Attribute ...
  // is N"), which is noisy on every sensor push. Keep our own tags at INFO.
  esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  gw_ir_init(IR_SEND_GPIO);
  dht_init(DHT_GPIO);

  // Restore last A/C state (or a sane default).
  if (!ac_store_load(&s_ac)) {
    s_ac = (gw_state_t){ .power = false, .mode = GW_COOL, .temp_c = 22,
                         .fan = GW_FAN_AUTO, .swing = GW_SWING_OFF,
                         .light = true, .turbo = false, .sleep = false,
                         .command = 0x00 };
  }

  // node::create registers the attribute + identify callbacks internally,
  // so no separate attribute::set_callback() is needed.
  node::config_t node_cfg;
  node_t *node = node::create(&node_cfg, attribute_cb, identification_cb);
  create_endpoints(node);

  // Matter-over-Thread: initialize the OpenThread platform before starting
  // Matter, or openthread_init_stack() aborts on a null platform config.
  // Values match ESP-IDF's default OT config: native 802.15.4 radio, no RCP
  // host, NVS-backed storage. (The ESP_OPENTHREAD_DEFAULT_* macros are only
  // defined in per-example headers, so the config is spelled out here.)
  esp_openthread_platform_config_t ot_config = {
      .radio_config = { .radio_mode = RADIO_MODE_NATIVE },
      .host_config = { .host_connection_mode = HOST_CONNECTION_MODE_NONE },
      .port_config = {
          .storage_partition_name = "nvs",
          .netif_queue_size = 10,
          .task_queue_size = 10,
      },
  };
  set_openthread_platform_config(&ot_config);

  // Register our custom passcode/discriminator provider with esp-matter BEFORE
  // start(); esp-matter installs it during init (needs
  // CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER=y).
  install_custom_commissionable_data();

  esp_matter::start(event_cb);

  // Print the pairing QR-code URL + manual code to serial ONLY when the device
  // isn't commissioned yet (no fabrics). Once paired there's no need to keep
  // printing the code on every boot.
  if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
    PrintOnboardingCodes(
        chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
  }

  // Factory-reset button (BOOT/GPIO9): input with pull-up, active-low.
  gpio_config_t btn_cfg = {
      .pin_bit_mask = 1ULL << RESET_BUTTON_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&btn_cfg);

  ESP_LOGI(TAG, "Matter-over-Thread A/C controller started");

  // Main loop: debounced IR send, queued fan changes, and reset-button hold.
  TickType_t press_start = 0;  // 0 = not currently pressed
  TickType_t dht_due = 0;      // next DHT read time (0 = read now)
  while (true) {
    if (s_apply_due != 0 && (int32_t)(xTaskGetTickCount() - s_apply_due) >= 0) {
      apply_to_ac();
    }
    if (s_pending_fan_percent >= 0) {
      int p = s_pending_fan_percent;
      s_pending_fan_percent = -1;
      apply_fan(p);
    }

    // Periodic DHT22 read -> feed Matter local temp + temp/humidity endpoints.
    if (dht_due == 0 || (int32_t)(xTaskGetTickCount() - dht_due) >= 0) {
      dht_due = xTaskGetTickCount() + pdMS_TO_TICKS(DHT_READ_MS);
      float t, h;
      if (dht_read(&t, &h)) {
        float adj = t - TEMP_OFFSET_C;
        ESP_LOGI(TAG, "DHT: %.1fC (raw %.1f), %.0f%%", adj, t, h);
        int16_t temp_centi = (int16_t)(adj * 100);   // MeasuredValue: 0.01C
        set_attr(s_temp_ep, kTempCluster,
                 TemperatureMeasurement::Attributes::MeasuredValue::Id,
                 esp_matter_nullable_int16(temp_centi));
        set_attr(s_thermostat_ep, kThermostatCluster,   // thermostat LocalTemperature
                 Thermostat::Attributes::LocalTemperature::Id,
                 esp_matter_nullable_int16(temp_centi));
        set_attr(s_humidity_ep, kHumidityCluster,        // MeasuredValue: 0.01%
                 RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
                 esp_matter_nullable_uint16((uint16_t)(h * 100)));
      }
    }

    // Hold BOOT for RESET_HOLD_MS -> factory reset (decommission + reboot).
    if (gpio_get_level((gpio_num_t)RESET_BUTTON_GPIO) == 0) {  // active-low
      if (press_start == 0) {
        press_start = xTaskGetTickCount();
      } else if ((xTaskGetTickCount() - press_start) >= pdMS_TO_TICKS(RESET_HOLD_MS)) {
        ESP_LOGW(TAG, "Reset button held - factory resetting Matter...");
        esp_matter::factory_reset();  // decommissions and reboots
      }
    } else {
      press_start = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
