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

#include <atomic>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_endpoint.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
// Matter-over-Thread: the OpenThread platform config must be set before start().
#include <esp_openthread_types.h>
#include <esp_ieee802154.h>  // esp_ieee802154_set_txpower (Thread range)
#include <platform/ESP32/OpenthreadLauncher.h>
#include <app/server/Server.h>  // re-open commissioning window on kFabricRemoved

#include "goodweather_ir.h"
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
#define TEMP_OFFSET_C      1.0     // subtract board-heat pickup (calibrated 2026-07-22)
// Factory-reset button: hold the BOOT button (GPIO9) this long to decommission.
#define RESET_BUTTON_GPIO  9
#define RESET_HOLD_MS      5000

// EXPERIMENT (2 rounds, verdict: keep 0): 1 = expose a single Room Air
// Conditioner endpoint (device type 0x0072) instead of separate thermostat +
// fan endpoints. Round 1 (2026-07-22, Heat+Cool only, bare FanControl): Apple
// rendered a plain thermostat and dropped the fan. Round 2 (2026-07-23,
// hardware-verified): exact Cptmeme/ESP-Matter-Daikin-S21 parity — AutoMode
// (feature_flags 35), TemperatureMeasurement on the endpoint, FanControl
// fan_mode_sequence 2 + Auto feature, OnOff<->SystemMode coupling. Controls
// WORK in the detail view, but the Home tile degrades to a generic grey
// "Matter Accessory" sensor tile (temp only, no mode/state, no fan) while
// plain-thermostat units render active "Cool to X" tiles. Apple-side device
// type limitation (cf. esp-matter#775); not fixable from firmware. The
// AutoMode feature + Auto=fan-only mapping were kept in mainline.
// NOTE: endpoint structure changes => factory reset + re-pair to test.
#define AC_ENDPOINT_EXPERIMENT 0

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

// Thermostat SystemMode enum values (Matter spec): 0=Off,1=Auto,3=Cool,4=Heat.
// Auto is only reachable when the AutoMode feature is enabled (experiment).
enum { MODE_OFF = 0, MODE_AUTO = 1, MODE_COOL = 3, MODE_HEAT = 4 };

// ---- Shadow state (equivalent to ThermostatAccessory's shadows) ----
static uint16_t s_thermostat_ep = 0;
static uint16_t s_fan_ep = 0;
static uint16_t s_temp_ep = 0;
static uint16_t s_humidity_ep = 0;

// The shadows below are written by attribute_cb on the Matter event-loop
// thread and read by the main-loop task, so they must be atomic. Setpoints
// are kept in Matter's native centi-degrees (int16, 0.01 C) — the old double
// representation could tear on this 32-bit core and gained nothing.
static gw_state_t s_ac;                              // current desired A/C state (main loop only)
static std::atomic<bool>       s_power{true};        // Room-AC experiment: OnOff cluster state
static std::atomic<uint8_t>    s_last_active_mode{MODE_COOL};  // last non-Off mode, for OnOff=true restore
static std::atomic<uint8_t>    s_system_mode{MODE_COOL};
static std::atomic<int16_t>    s_cool_centi{2200};   // OccupiedCoolingSetpoint
static std::atomic<int16_t>    s_heat_centi{1950};   // OccupiedHeatingSetpoint
static std::atomic<int>        s_pending_fan_percent{-1};
static std::atomic<int>        s_last_fan_percent{-1};  // last applied fan %, for change detection
static std::atomic<TickType_t> s_apply_due{0};       // debounce deadline; 0 = not scheduled
static std::atomic<TickType_t> s_fan_snap_due{0};    // deferred fan->Off snap-back; 0 = not scheduled

// ---- IR emission: build gw_state from shadows and send ----
static void apply_to_ac() {
  // Clear the deadline BEFORE reading the shadows: if the Matter thread
  // changes a value right after we read it, it also re-arms s_apply_due and
  // the main loop fires again — worst case one redundant (debounced) send,
  // never a lost change.
  s_apply_due = 0;

  uint8_t mode = s_system_mode;
  if (mode == MODE_OFF || !s_power.load()) {
    s_ac.power = false;
    s_ac.command = 0x00;  // kGoodweatherCmdPower
    ESP_LOGI(TAG, "AC -> OFF");
    gw_ir_send(&s_ac);

    // Thermostat off => the AC's fan is off too. Reflect that on the fan
    // endpoint so Apple Home doesn't keep showing a running fan. Setting
    // PercentSetting to 0 and FanMode to Off; guard against our own write
    // re-queuing an IR send via the change-detection state.
    s_last_fan_percent = 0;
    set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentSetting::Id,
             esp_matter_nullable_uint8(0));
    set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::FanMode::Id,
             esp_matter_enum8(0));  // FanModeEnum::kOff
    set_attr(s_thermostat_ep, kThermostatCluster,
             Thermostat::Attributes::ThermostatRunningState::Id,
             esp_matter_bitmap16(0));  // idle
    return;
  }

  s_ac.power = true;
  // Heat/Cool use their own setpoint. Auto is mapped to the AC's FAN-ONLY
  // mode (Boris's mapping, 2026-07-23): Apple's dial has no fan-only option,
  // so "Auto" doubles as "just blow air". The AC ignores the temperature in
  // fan mode; send the cool setpoint to keep the frame well-formed.
  int16_t target_centi = (mode == MODE_HEAT) ? s_heat_centi.load() : s_cool_centi.load();
  if (target_centi < AC_MIN_TEMP_C * 100) target_centi = AC_MIN_TEMP_C * 100;
  if (target_centi > AC_MAX_TEMP_C * 100) target_centi = AC_MAX_TEMP_C * 100;
  s_ac.temp_c = (uint8_t)((target_centi + 50) / 100);  // round to nearest degree

  switch (mode) {
    case MODE_HEAT: s_ac.mode = GW_HEAT; s_ac.command = 0x01; break;
    case MODE_COOL: s_ac.mode = GW_COOL; s_ac.command = 0x01; break;
    case MODE_AUTO: s_ac.mode = GW_FAN;  s_ac.command = 0x01; break;  // fan-only
    default:        s_ac.mode = GW_COOL; s_ac.command = 0x01; break;
  }
  ESP_LOGI(TAG, "AC -> mode %d, target %dC", s_ac.mode, s_ac.temp_c);
  gw_ir_send(&s_ac);

  // Reflect the commanded activity: bit0=Heat, bit1=Cool, bit2=Fan running.
  // Auto is fan-only, so neither heat nor cool is reported — fan bit only.
  uint16_t running =
      0x0004 | (mode == MODE_HEAT ? 0x0001 : mode == MODE_COOL ? 0x0002 : 0);
  set_attr(s_thermostat_ep, kThermostatCluster,
           Thermostat::Attributes::ThermostatRunningState::Id,
           esp_matter_bitmap16(running));
}

static void schedule_apply() {
  s_apply_due = xTaskGetTickCount() + pdMS_TO_TICKS(AC_SEND_DEBOUNCE_MS);
  if (s_apply_due == 0) s_apply_due = 1;
}

// ---- Attribute write callback: controller changed something ----
static esp_err_t attribute_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                              uint32_t cluster_id, uint32_t attribute_id,
                              esp_matter_attr_val_t *val, void *priv) {
  // NOTE on fan-while-off strategies (all hardware-tested 2026-07-23):
  // rejecting the write at PRE_UPDATE does NOT work with Apple Home — the
  // value never changes, esp-matter skips reporting unchanged values
  // (ESP_ERR_NOT_FINISHED in update_or_report), so the app's optimistic "on"
  // UI persists until its periodic re-sync (1-2 min). The working approach is
  // commit-then-revert: accept the write, then flip the value back 250 ms
  // later — a real transition that reports immediately. See the fan guard in
  // the POST_UPDATE handler below.
  if (type != attribute::POST_UPDATE) {
    return ESP_OK;
  }

#if AC_ENDPOINT_EXPERIMENT
  // Room-AC experiment: the OnOff cluster is the AC's power switch.
  if (endpoint_id == s_thermostat_ep && cluster_id == OnOff::Id &&
      attribute_id == OnOff::Attributes::OnOff::Id) {
    bool on = val->val.b;
    if (on != s_power.load()) {
      s_power = on;
      ESP_LOGI(TAG, "Power -> %s", on ? "ON" : "OFF");
      // OnOff=true while SystemMode is Off would still IR-send OFF (apply_to_ac
      // keys on the mode) — restore the last active mode so the toggle really
      // turns the AC on. s_system_mode is set BEFORE the attribute write, so
      // the SystemMode POST_UPDATE this triggers no-ops on change detection.
      if (on && s_system_mode.load() == MODE_OFF) {
        uint8_t m = s_last_active_mode.load();
        s_system_mode = m;
        set_attr(s_thermostat_ep, kThermostatCluster,
                 Thermostat::Attributes::SystemMode::Id, esp_matter_enum8(m));
      }
      schedule_apply();
    }
    return ESP_OK;
  }
#endif

  if (endpoint_id == s_thermostat_ep && cluster_id == kThermostatCluster) {
    switch (attribute_id) {
      case Thermostat::Attributes::SystemMode::Id: {
        // Only act on a REAL change. POST_UPDATE also fires when the app
        // re-writes the same value on reconnect/sync; without this guard that
        // re-triggers the AC with identical settings every time you reopen Home.
        // MUST NOT call apply_to_ac() here (Matter event-loop thread; gw_ir_send
        // blocks) - defer to the main loop via schedule_apply().
        uint8_t m = val->val.u8;
        if (m != s_system_mode.load()) {
          s_system_mode = m;
          ESP_LOGI(TAG, "SystemMode -> %d", m);
#if AC_ENDPOINT_EXPERIMENT
          // Keep OnOff in lockstep with the mode. Apple's tile power state
          // follows the OnOff cluster, and apply_to_ac() gates on s_power —
          // without this, picking Cool while OnOff=false IR-sends OFF and the
          // tile never shows the AC as on (observed 2026-07-23). s_power is
          // set BEFORE the write, so the OnOff POST_UPDATE no-ops.
          if (m != MODE_OFF) s_last_active_mode = m;
          bool want_on = (m != MODE_OFF);
          if (want_on != s_power.load()) {
            s_power = want_on;
            set_attr(s_thermostat_ep, OnOff::Id, OnOff::Attributes::OnOff::Id,
                     esp_matter_bool(want_on));
          }
#endif
          schedule_apply();
        }
        break;
      }
      case Thermostat::Attributes::OccupiedCoolingSetpoint::Id: {
        int16_t v = val->val.i16;
        if (v != s_cool_centi.load()) {
          s_cool_centi = v;
          ESP_LOGI(TAG, "Cool setpoint -> %.1f", v / 100.0);
          schedule_apply();
        }
        break;
      }
      case Thermostat::Attributes::OccupiedHeatingSetpoint::Id: {
        int16_t v = val->val.i16;
        if (v != s_heat_centi.load()) {
          s_heat_centi = v;
          ESP_LOGI(TAG, "Heat setpoint -> %.1f", v / 100.0);
          schedule_apply();
        }
        break;
      }
      default: break;
    }
  } else if (endpoint_id == s_fan_ep && cluster_id == kFanCluster) {
    // Thermostat Off => the AC (and its fan) are off. A fan write here must
    // NOT reach the AC (no IR) — just snap the app's fan UI back to Off.
    // Our own zero write-backs re-enter this callback with harmless values.
    if (s_system_mode.load() == MODE_OFF) {
      bool wants_on =
          (attribute_id == FanControl::Attributes::PercentSetting::Id &&
           !(val->type == ESP_MATTER_VAL_TYPE_NULLABLE_UINT8 && val->val.u8 == 0xFF) &&
           val->val.u8 != 0) ||
          (attribute_id == FanControl::Attributes::FanMode::Id && val->val.u8 != 0);
      if (wants_on) {
        // Commit-then-revert: the write has just committed (so the attribute
        // holds the controller's value), no IR is queued, and the snap-back
        // below reverts it to 0 shortly after — a REAL value transition, which
        // is the only thing that produces an immediate report (see NOTE at the
        // top of this callback). Don't write back inline: that lands inside
        // the controller's write transaction and is ignored by Apple Home.
        // Arm only if not already armed so repeat attempts don't postpone it.
        ESP_LOGI(TAG, "Fan write while thermostat off - revert scheduled");
        s_last_fan_percent = 0;
        TickType_t expected = 0;
        TickType_t due = xTaskGetTickCount() + pdMS_TO_TICKS(250);
        if (due == 0) due = 1;
        s_fan_snap_due.compare_exchange_strong(expected, due);
      }
      return ESP_OK;
    }
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
      if (snapped != s_last_fan_percent.load()) {
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
    } else if (attribute_id == FanControl::Attributes::FanMode::Id) {
      // Controllers may write FanMode (Off/Low/Med/High/On/Auto) instead of a
      // percent — Google Home in particular, and Apple's fan on/off toggle.
      // Map the mode onto the AC's discrete speeds and reuse the same
      // change-detection/queue path as PercentSetting.
      uint8_t m = val->val.u8;
      int snapped;
      switch (m) {
        case 1:  snapped = 33; break;  // Low
        case 2:  snapped = 66; break;  // Medium
        case 3:  snapped = 99; break;  // High
        case 4:  snapped = 99; break;  // On -> treat as High
        default: snapped = 0;  break;  // Off/Auto/Smart -> AC's auto fan
      }
      if (snapped != s_last_fan_percent.load()) {
        s_last_fan_percent = snapped;
        s_pending_fan_percent = snapped;
        ESP_LOGI(TAG, "FanMode %u -> fan %d%%", m, snapped);
        // Keep the percent attributes consistent with the mode the
        // controller set (the PercentSetting handler no-ops on this write
        // because s_last_fan_percent already matches).
        set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentSetting::Id,
                 esp_matter_nullable_uint8((uint8_t)snapped));
        set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentCurrent::Id,
                 esp_matter_uint8((uint8_t)snapped));
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
#if AC_ENDPOINT_EXPERIMENT
  // Room Air Conditioner (0x0072), Daikin-S21-parity config: OnOff + Thermostat
  // with Heat|Cool|Auto (feature_flags = 35, exactly what Daikin sets);
  // FanControl and TemperatureMeasurement are added onto the SAME endpoint
  // below. AutoMode brings back the deadband coupling (cool >= heat +
  // deadband), but esp-matter's default MinSetpointDeadBand is only 0.2 C.
  endpoint::room_air_conditioner::config_t ac_cfg;
  ac_cfg.on_off.on_off = false;  // Daikin inits power off
  ac_cfg.thermostat.control_sequence_of_operation = 4;  // cooling & heating
  ac_cfg.thermostat.system_mode = s_system_mode;
  ac_cfg.thermostat.feature_flags = cluster::thermostat::feature::cooling::get_id() |
                                    cluster::thermostat::feature::heating::get_id() |
                                    cluster::thermostat::feature::auto_mode::get_id();
  endpoint_t *th = endpoint::room_air_conditioner::create(node, &ac_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_thermostat_ep = endpoint::get_id(th);
#else
  // Thermostat: the base cluster's create() asserts that the Heating/Cooling
  // feature bits are already declared in feature_flags, so set them BEFORE
  // create(). Setpoints then come from the feature configs added afterward.
  endpoint::thermostat::config_t th_cfg;
  th_cfg.thermostat.control_sequence_of_operation = 4;  // cooling & heating
  th_cfg.thermostat.system_mode = s_system_mode;
  // Cooling + Heating + Auto. Auto is mapped to the AC's FAN-ONLY mode in
  // apply_to_ac() (Apple's dial has no fan-only option, so Auto doubles as
  // "just blow air"). AutoMode's deadband coupling (cool >= heat + deadband)
  // once made us avoid it, but esp-matter's default MinSetpointDeadBand is
  // 0.2 C — negligible. Modes: Off/Cool/Heat/Auto.
  th_cfg.thermostat.feature_flags = cluster::thermostat::feature::cooling::get_id() |
                                    cluster::thermostat::feature::heating::get_id() |
                                    cluster::thermostat::feature::auto_mode::get_id();
  endpoint_t *th = endpoint::thermostat::create(node, &th_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_thermostat_ep = endpoint::get_id(th);
#endif

  cluster_t *th_cluster = cluster::get(th, kThermostatCluster);
  cluster::thermostat::feature::cooling::config_t cool_feat;
  cool_feat.occupied_cooling_setpoint = s_cool_centi.load();
  cluster::thermostat::feature::cooling::add(th_cluster, &cool_feat);
  cluster::thermostat::feature::heating::config_t heat_feat;
  heat_feat.occupied_heating_setpoint = s_heat_centi.load();
  cluster::thermostat::feature::heating::add(th_cluster, &heat_feat);
  // AutoMode feature: adds ThermostatRunningMode and MinSetpointDeadBand
  // (esp-matter default 0.2 C) and makes SystemMode=Auto legal.
  cluster::thermostat::feature::auto_mode::config_t auto_feat;
  cluster::thermostat::feature::auto_mode::add(th_cluster, &auto_feat);

  // Setpoint limits: declare the AC's real range (16..31 C) so controllers
  // bound the dial UI, and the thermostat cluster server rejects
  // out-of-range writes per spec — ends the silent mismatch where Home
  // offered temperatures the device then clamped. (Same pattern as CHIP's
  // reference thermostat example.)
  cluster::thermostat::attribute::create_abs_min_cool_setpoint_limit(th_cluster, AC_MIN_TEMP_C * 100);
  cluster::thermostat::attribute::create_abs_max_cool_setpoint_limit(th_cluster, AC_MAX_TEMP_C * 100);
  cluster::thermostat::attribute::create_min_cool_setpoint_limit(th_cluster, AC_MIN_TEMP_C * 100);
  cluster::thermostat::attribute::create_max_cool_setpoint_limit(th_cluster, AC_MAX_TEMP_C * 100);
  cluster::thermostat::attribute::create_abs_min_heat_setpoint_limit(th_cluster, AC_MIN_TEMP_C * 100);
  cluster::thermostat::attribute::create_abs_max_heat_setpoint_limit(th_cluster, AC_MAX_TEMP_C * 100);
  cluster::thermostat::attribute::create_min_heat_setpoint_limit(th_cluster, AC_MIN_TEMP_C * 100);
  cluster::thermostat::attribute::create_max_heat_setpoint_limit(th_cluster, AC_MAX_TEMP_C * 100);

  // Running state (bitmap16: bit0=Heat, bit1=Cool, bit2=Fan). Shows
  // "actively cooling/heating" vs idle in controllers. Kept in sync by
  // apply_to_ac() from the commanded state.
  cluster::thermostat::attribute::create_thermostat_running_state(th_cluster, 0);

  // NOTE on humidity placement: both a bare RelativeHumidityMeasurement
  // cluster on the thermostat endpoint AND a composed humidity device type on
  // it were tried — Apple Home ignored both (verified empirically 2026-07).
  // What works (proven by the old Arduino build) is a SEPARATE humidity
  // endpoint whose MeasuredValue starts NON-NULL — Apple hides sensors that
  // report null at commissioning time. See create of the humidity endpoint
  // below, initialized to 50% exactly like the Arduino build did.

#if AC_ENDPOINT_EXPERIMENT
  // FanControl on the same Room-AC endpoint, configured exactly like Daikin:
  // FanMode Auto, fan_mode_sequence 2 (Off/Low/Med/High/Auto), Auto feature.
  // The fan attribute handling keys on s_fan_ep + cluster id, so aliasing the
  // endpoint id keeps the same code path.
  cluster::fan_control::config_t fan_cl_cfg;
  fan_cl_cfg.fan_mode = 5;           // FanModeEnum::kAuto
  fan_cl_cfg.fan_mode_sequence = 2;  // Off/Low/Med/High/Auto
  cluster_t *fan_cl = cluster::fan_control::create(th, &fan_cl_cfg, CLUSTER_FLAG_SERVER);
  cluster::fan_control::feature::fan_auto::add(fan_cl);
  s_fan_ep = s_thermostat_ep;

  // TemperatureMeasurement on the same endpoint (third Daikin delta). Non-null
  // initial value per the Apple lesson; the DHT overwrites it within ~30 s.
  cluster::temperature_measurement::config_t ac_temp_cfg;
  ac_temp_cfg.measured_value = nullable<int16_t>(2500);  // 25.00 C
  cluster::temperature_measurement::create(th, &ac_temp_cfg, CLUSTER_FLAG_SERVER);
#else
  // Fan
  endpoint::fan::config_t fan_cfg;
  endpoint_t *fan = endpoint::fan::create(node, &fan_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_fan_ep = endpoint::get_id(fan);
#endif

  // Temperature (local temp source). Non-null initial value: Apple Home is
  // known to hide/zero sensors that are null at commissioning (see
  // esp-matter#265); the DHT overwrites it within ~30s anyway.
  endpoint::temperature_sensor::config_t temp_cfg;
  temp_cfg.temperature_measurement.measured_value = nullable<int16_t>(2500);  // 25.00 C
  endpoint_t *temp = endpoint::temperature_sensor::create(node, &temp_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_temp_ep = endpoint::get_id(temp);

  // Humidity: separate endpoint, non-null initial 50% — the exact layout the
  // old Arduino build used (humidity.begin(50.0)), which displayed in Apple
  // Home. Composed/thermostat-endpoint variants did not (see note above).
  endpoint::humidity_sensor::config_t hum_cfg;
  hum_cfg.relative_humidity_measurement.measured_value = nullable<uint16_t>(5000);  // 50.00 %
  endpoint_t *hum = endpoint::humidity_sensor::create(node, &hum_cfg, ENDPOINT_FLAG_NONE, NULL);
  s_humidity_ep = endpoint::get_id(hum);

  ESP_LOGI(TAG, "Endpoints: thermostat=%d fan=%d temp=%d humidity=%d",
           s_thermostat_ep, s_fan_ep, s_temp_ep, s_humidity_ep);
}

// ---- Boot sync: pull esp-matter's persisted attribute values into the shadows ----
// SystemMode and both setpoints are ATTRIBUTE_FLAG_NONVOLATILE, so esp-matter
// restores the user's last values from its own NVS at boot — overriding the
// initial values we passed to create_endpoints(). Without this sync the shadows
// keep their compile-time defaults and the first IR frame after a reboot is
// built from a stale mode/setpoint mix.
static void sync_shadows_from_matter() {
  esp_matter_attr_val_t v = esp_matter_invalid(NULL);
  if (attribute::get_val(s_thermostat_ep, kThermostatCluster,
                         Thermostat::Attributes::SystemMode::Id, &v) == ESP_OK) {
    s_system_mode = v.val.u8;
  }
#if AC_ENDPOINT_EXPERIMENT
  if (attribute::get_val(s_thermostat_ep, OnOff::Id,
                         OnOff::Attributes::OnOff::Id, &v) == ESP_OK) {
    s_power = v.val.b;
  }
  // Reconcile: OnOff and SystemMode are kept in lockstep by attribute_cb, but
  // NVS may hold a mismatched pair (e.g. persisted by a build without the
  // coupling). SystemMode is the user's last real intent — make OnOff follow.
  if (s_system_mode.load() != MODE_OFF) {
    s_last_active_mode = s_system_mode.load();
    if (!s_power.load()) {
      s_power = true;
      set_attr(s_thermostat_ep, OnOff::Id, OnOff::Attributes::OnOff::Id,
               esp_matter_bool(true));
    }
  }
#endif
  if (attribute::get_val(s_thermostat_ep, kThermostatCluster,
                         Thermostat::Attributes::OccupiedCoolingSetpoint::Id, &v) == ESP_OK) {
    s_cool_centi = v.val.i16;
  }
  if (attribute::get_val(s_thermostat_ep, kThermostatCluster,
                         Thermostat::Attributes::OccupiedHeatingSetpoint::Id, &v) == ESP_OK) {
    s_heat_centi = v.val.i16;
  }
  // Fan: PercentSetting is NOT persisted by Matter, but FanMode IS
  // (NONVOLATILE). Restore the fan speed from it — this runs before the main
  // loop starts, so touching s_ac here is single-threaded. Matter's own NVS
  // is the single source of reboot truth for ALL dynamic state (mode,
  // setpoints, fan); swing/light/turbo are compile-time constants.
  if (attribute::get_val(s_fan_ep, kFanCluster,
                         FanControl::Attributes::FanMode::Id, &v) == ESP_OK) {
    switch (v.val.u8) {           // FanModeEnum
      case 1:  s_ac.fan = GW_FAN_LOW;  s_last_fan_percent = 33; break;  // Low
      case 2:  s_ac.fan = GW_FAN_MED;  s_last_fan_percent = 66; break;  // Medium
      case 3:                                                           // High
      case 4:  s_ac.fan = GW_FAN_HIGH; s_last_fan_percent = 99; break;  // On
      default: s_ac.fan = GW_FAN_AUTO; s_last_fan_percent = 0;  break;  // Off/Auto
    }
  }
  ESP_LOGI(TAG, "Shadows synced: mode=%d cool=%.1f heat=%.1f fan%%=%d",
           s_system_mode.load(), s_cool_centi.load() / 100.0,
           s_heat_centi.load() / 100.0, s_last_fan_percent.load());
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

  // Baseline A/C state. The dynamic fields (mode/temp/fan) are restored from
  // Matter's own NVS by sync_shadows_from_matter() after start() — no separate
  // persistence needed. The rest are fixed defaults.
  s_ac = (gw_state_t){ .power = false, .mode = GW_COOL, .temp_c = 22,
                       .fan = GW_FAN_AUTO, .swing = GW_SWING_OFF,
                       .light = true, .turbo = false, .sleep = false,
                       .command = 0x00 };

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

  // Thread range: raise the 802.15.4 TX power to the C6's maximum, +20 dBm
  // (stack default is well below). Boris's choice for maximum range; note the
  // trade-offs: with the PCB antenna's ~2 dBi the EIRP nudges the 2.4 GHz
  // 100 mW limit, and TX reach can exceed RX reach (asymmetric links). Drop
  // to 15 if mesh links look flaky. Must run AFTER start() — the OpenThread
  // launch initializes the radio and would otherwise override this.
  esp_ieee802154_set_txpower(20);
  ESP_LOGI(TAG, "802.15.4 TX power: %d dBm", esp_ieee802154_get_txpower());

  // Adopt the persisted mode/setpoints (restored by esp-matter from NVS) so the
  // shadows match what the controller sees. Sync only — no IR send at boot: the
  // AC itself wasn't power-cycled just because the ESP rebooted.
  sync_shadows_from_matter();

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
  // First DHT read waits ~2.5s: the sensor needs ~2s after power-up before it
  // responds, so an immediate read always timed out and logged a warning.
  TickType_t dht_due = xTaskGetTickCount() + pdMS_TO_TICKS(2500);
  bool dht_retry_used = false;  // one quick retry per failed read cycle
  while (true) {
    TickType_t due = s_apply_due.load();
    if (due != 0 && (int32_t)(xTaskGetTickCount() - due) >= 0) {
      apply_to_ac();
    }
    // exchange() so a fan write landing between "read" and "clear" can't be
    // lost — the old two-step read-then-store had that window. Mode Off also
    // drops any queued fan send (belt to the callback-level guard): the AC is
    // off, no IR should go out for the fan.
    int p = s_pending_fan_percent.exchange(-1);
    if (p >= 0 && s_system_mode.load() != MODE_OFF) {
      apply_fan(p);
    }

    // Deferred fan->Off snap-back (fan touched while thermostat is Off): runs
    // after the controller's write transaction has closed, so the revert is
    // reported as a fresh change the app actually renders.
    TickType_t snap = s_fan_snap_due.load();
    if (snap != 0 && (int32_t)(xTaskGetTickCount() - snap) >= 0) {
      s_fan_snap_due = 0;
      s_last_fan_percent = 0;
      ESP_LOGI(TAG, "Fan snap-back: reverting fan attributes to Off");
      set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentSetting::Id,
               esp_matter_nullable_uint8(0));
      set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::PercentCurrent::Id,
               esp_matter_uint8(0));
      set_attr(s_fan_ep, kFanCluster, FanControl::Attributes::FanMode::Id,
               esp_matter_enum8(0));  // FanModeEnum::kOff
    }

    // Periodic DHT22 read -> feed Matter local temp + temp/humidity endpoints.
    // On failure, retry once ~2.5s later (the sensor needs ~2s between reads)
    // so a single bad cycle doesn't leave a 60s gap in the readings.
    if ((int32_t)(xTaskGetTickCount() - dht_due) >= 0) {
      float t, h;
      bool dht_ok = dht_read(&t, &h);
      if (!dht_ok && !dht_retry_used) {
        dht_retry_used = true;
        dht_due = xTaskGetTickCount() + pdMS_TO_TICKS(2500);
      } else {
        dht_retry_used = false;
        dht_due = xTaskGetTickCount() + pdMS_TO_TICKS(DHT_READ_MS);
      }
      if (dht_ok) {
        float adj = t - TEMP_OFFSET_C;
        ESP_LOGI(TAG, "DHT: %.1fC (raw %.1f), %.0f%%", adj, t, h);
        int16_t temp_centi = (int16_t)(adj * 100);   // MeasuredValue: 0.01C
        set_attr(s_temp_ep, kTempCluster,
                 TemperatureMeasurement::Attributes::MeasuredValue::Id,
                 esp_matter_nullable_int16(temp_centi));
        set_attr(s_thermostat_ep, kThermostatCluster,   // thermostat LocalTemperature
                 Thermostat::Attributes::LocalTemperature::Id,
                 esp_matter_nullable_int16(temp_centi));
#if AC_ENDPOINT_EXPERIMENT
        set_attr(s_thermostat_ep, kTempCluster,          // Room-AC endpoint's own temp cluster
                 TemperatureMeasurement::Attributes::MeasuredValue::Id,
                 esp_matter_nullable_int16(temp_centi));
#endif
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
