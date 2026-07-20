#include "goodweather_ir.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gw_ir";

// Timings (microseconds) from ir_Goodweather.h
#define GW_HDR_MARK    6820
#define GW_HDR_SPACE   6820
#define GW_BIT_MARK    580
#define GW_ONE_SPACE   580
#define GW_ZERO_SPACE  1860
#define GW_MSG_GAP     20000   // kDefaultMessageGap-ish trailing gap

#define GW_BITS        48
#define GW_STATE_INIT  0xD50000000000ULL

// Bitfield offsets within the 48-bit frame (byte.bit), derived from the
// GoodweatherProtocol union. Byte 0 is bits 0..7, byte 1 is 8..15, etc.
//   Byte1: Light(bit8), Turbo(bit11)
//   Byte2: Command(bits16..19)
//   Byte3: Sleep(24), Power(25), Swing(26..27), AirFlow(28), Fan(29..30)
//   Byte4: Temp(32..35), Mode(37..39)
#define SET_FIELD(raw, val, shift, mask) \
  do { (raw) &= ~((uint64_t)(mask) << (shift)); \
       (raw) |= ((uint64_t)((val) & (mask)) << (shift)); } while (0)

// RMT runs at 1 MHz (1 tick = 1 us) so timings map directly to durations.
#define GW_RMT_RESOLUTION_HZ 1000000

static rmt_channel_handle_t s_tx_chan = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;

uint64_t gw_ir_build_raw(const gw_state_t *st) {
  uint64_t raw = GW_STATE_INIT;
  SET_FIELD(raw, st->command, 16, 0x0F);
  SET_FIELD(raw, st->light ? 1 : 0, 8, 0x01);
  SET_FIELD(raw, st->turbo ? 1 : 0, 11, 0x01);
  SET_FIELD(raw, st->sleep ? 1 : 0, 24, 0x01);
  SET_FIELD(raw, st->power ? 1 : 0, 25, 0x01);
  SET_FIELD(raw, st->swing, 26, 0x03);
  SET_FIELD(raw, st->fan, 29, 0x03);
  uint8_t temp = st->temp_c;
  if (temp < 16) temp = 16;
  if (temp > 31) temp = 31;
  SET_FIELD(raw, (uint8_t)(temp - 16), 32, 0x0F);
  SET_FIELD(raw, st->mode, 37, 0x07);
  return raw;
}

void gw_ir_init(int gpio_num) {
  // mem_block_symbols = 64 matches ESP-IDF's own IR transceiver example. The C6
  // has no RMT DMA, so long frames (~99 symbols) stream via the driver's ISR
  // ping-pong refill. Setting this to exactly 48 (the full hardware block)
  // leaves no room for that refill and the transmission stalls
  // (rmt_tx_wait_all_done times out); 64 gives the driver the buffering it needs.
  // 256 was too large and failed channel allocation ("no free tx channels").
  rmt_tx_channel_config_t tx_cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = GW_RMT_RESOLUTION_HZ,
      .mem_block_symbols = 64,
      .trans_queue_depth = 4,
      .gpio_num = gpio_num,
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_tx_chan));

  // 38 kHz, 33% duty carrier on the marks.
  rmt_carrier_config_t carrier = {
      .frequency_hz = 38000,
      .duty_cycle = 0.33,
  };
  ESP_ERROR_CHECK(rmt_apply_carrier(s_tx_chan, &carrier));

  rmt_copy_encoder_config_t copy_cfg = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_cfg, &s_copy_encoder));

  ESP_ERROR_CHECK(rmt_enable(s_tx_chan));
  ESP_LOGI(TAG, "Goodweather RMT IR initialized on GPIO %d", gpio_num);
}

// Append one rmt_symbol_word_t (mark then space) to the buffer.
static inline void push_symbol(rmt_symbol_word_t *buf, size_t *n,
                               uint16_t mark_us, uint16_t space_us) {
  buf[*n].level0 = 1;
  buf[*n].duration0 = mark_us;
  buf[*n].level1 = 0;
  buf[*n].duration1 = space_us;
  (*n)++;
}

void gw_ir_send(const gw_state_t *st) {
  if (!s_tx_chan) {
    ESP_LOGE(TAG, "gw_ir_send before init");
    return;
  }
  uint64_t raw = gw_ir_build_raw(st);

  // Worst case: header(1) + 6 bytes * 16 bits(96) + footer(2) = 99 symbols.
  static rmt_symbol_word_t symbols[128];
  size_t n = 0;

  // Header
  push_symbol(symbols, &n, GW_HDR_MARK, GW_HDR_SPACE);

  // Data: for each of 6 bytes, send byte then inverted byte, LSB-first, 16 bits.
  for (int i = 0; i < GW_BITS; i += 8) {
    uint16_t chunk = (raw >> i) & 0xFF;
    chunk = ((uint16_t)(~chunk) << 8) | chunk;  // inverted copy in high byte
    for (int b = 0; b < 16; b++) {
      bool one = (chunk >> b) & 0x1;
      push_symbol(symbols, &n, GW_BIT_MARK, one ? GW_ONE_SPACE : GW_ZERO_SPACE);
    }
  }

  // Footer: mark, long space, then a final mark whose trailing level has
  // duration 0. That zero-duration entry is the RMT end-of-transmission marker;
  // without it the channel never signals "done" and rmt_tx_wait_all_done times
  // out (the "flush timeout" we were hitting). The inter-message gap is enforced
  // by the caller's send cadence instead of a trailing space here.
  push_symbol(symbols, &n, GW_BIT_MARK, GW_HDR_SPACE);
  symbols[n].level0 = 1;
  symbols[n].duration0 = GW_BIT_MARK;
  symbols[n].level1 = 0;
  symbols[n].duration1 = 0;   // EOT marker
  n++;

  // Fire-and-forget: rmt_transmit() enqueues the frame (trans_queue_depth=4)
  // and the driver serializes back-to-back sends, so no blocking wait is
  // needed. Log-and-return on error rather than ESP_ERROR_CHECK (which would
  // abort/reboot) — an IR glitch must never take down the Matter/Thread stack.
  rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
  esp_err_t err = rmt_transmit(s_tx_chan, s_copy_encoder, symbols,
                               n * sizeof(rmt_symbol_word_t), &tx_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGI(TAG, "Queued Goodweather frame raw=0x%012llX (%d symbols)",
           (unsigned long long)raw, (int)n);
}
