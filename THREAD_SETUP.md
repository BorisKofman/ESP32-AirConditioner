# Matter-over-Thread build (ESP32-C6) — native ESP-IDF

This is a **pure ESP-IDF + esp-matter** firmware (no Arduino core). It exists
because Matter-over-Thread on the C6 cannot be built with the Arduino core:

- The Arduino Matter library is precompiled **WiFi-only**.
- Building Matter-over-Thread from source needs **esp-matter**. This project is
  built with **esp-matter 1.5 + ESP-IDF v5.5.4** (the documented compatible
  pair). Do not mix versions: esp-matter is tightly locked to a specific IDF
  release, and a mismatch causes deep CHIP compile errors.
- Every `arduino-esp32` release either requires IDF >=5.3 (incompatible with
  esp-matter) or is 3.0.0 (no C6 UART support). There is **no** Arduino version
  that builds Matter-over-Thread on the C6.

So the Arduino sketch was reimplemented natively. `IRremoteESP8266` (Arduino-only)
was replaced by a native RMT encoder for the **Goodweather** protocol, which is
what this AC uses.

## What's in `idf/`

| File | Role |
|---|---|
| `main/main.cpp` | `app_main`: creates the Matter thermostat/fan/temperature/humidity endpoints, handles attribute writes, drives IR |
| `main/goodweather_ir.{c,h}` | Native RMT Goodweather IR encoder (ported from IRremoteESP8266's `ir_Goodweather.cpp`) |
| `main/ac_store.{c,h}` | NVS persistence of the last A/C state (replaces Arduino Preferences) |
| `sdkconfig.defaults` | Thread on, WiFi station off, FreeRTOS 1000 Hz, ClosureControl cluster excluded |
| `main/idf_component.yml` | Depends on `espressif/esp_matter` only (no Arduino) |

## Prerequisites

- **A Thread Border Router** on your network (HomePod mini / Apple TV 4K,
  Nest Hub 2nd gen / Nest Wifi Pro, Echo 4th gen, or DIY). Mandatory — without
  one the device cannot be commissioned or reached.
- **ESP-IDF v5.5.4** installed at `~/esp/esp-idf-v5.5.4` (already done on this
  machine).

## Build

```bash
. ~/esp/esp-idf-v5.5.4/export.sh
cd idf
idf.py set-target esp32c6      # first time; applies sdkconfig.defaults
idf.py build
```

Produces `build/esp32-airconditioner-thread.bin` (~1.9 MB).

Confirmed-correct transport config (checked in the generated `sdkconfig`):
`CONFIG_ENABLE_MATTER_OVER_THREAD=y`, `CONFIG_ENABLE_WIFI_STATION` **unset**,
`CONFIG_OPENTHREAD_ENABLED=y`, `CONFIG_IEEE802154_ENABLED=y`.

## Flash

```bash
ls /dev/tty.usbmodem*                 # find the port (plug the board in)
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

## Commission over Thread

On first boot the device advertises over BLE. Add it in Apple Home / Google
Home; the commissioner pushes your Thread credentials over BLE and the device
joins the mesh via the Border Router. The pairing passcode/discriminator is
currently the esp-matter default test values — set custom ones in `main.cpp`
before production (the old `CustomCommissionableData.h` logic can be ported).

## Known gaps / TODO (not yet implemented in the native port)

1. **DHT22 sensor** — `main.cpp` creates the temperature & humidity endpoints
   but does not yet read the DHT. Port a native DHT read (RMT or bit-bang GPIO)
   and call `attribute::update()` on the temperature/humidity clusters.
2. **IR receive / protocol learning** — the Arduino version learned the protocol
   from the remote. The native version **hardcodes Goodweather**. RX/learning is
   not ported (would need a native RMT RX + Goodweather decoder).
3. **Custom commissioning identity** — port the passcode/discriminator from the
   old `CustomCommissionableData.h`.
4. **Goodweather frame not hardware-verified** — the encoder is a bit-accurate
   port of the library, but it has NOT been confirmed against the real AC. Test
   with the unit before trusting it.
5. **GPIO pins** — `IR_SEND_GPIO` (4) and future DHT pin must match your C6 board.

## Going back to WiFi

The original Arduino sketch (root `.ino` + `Config.h` with `USE_THREAD 0`) still
builds Matter-over-WiFi in the Arduino IDE on the C6, unchanged.
