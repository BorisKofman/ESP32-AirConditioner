// Custom commissioning identity for the native build: a fixed custom passcode
// and discriminator (instead of CHIP's shared test values), with the SPAKE2+
// verifier derived from the passcode at runtime (no offline spake2p tooling).
// Install via install_custom_commissionable_data() BEFORE esp_matter::start().
//
// (Originally ported from the Arduino build; that build has since been removed
// and this native provider is the single source of the pairing identity.)
#pragma once
#include <platform/CommissionableDataProvider.h>
#include <crypto/CHIPCryptoPAL.h>
#include <lib/support/Span.h>
#include <esp_matter_providers.h>  // esp_matter::set_custom_commissionable_data_provider
#include <esp_mac.h>               // esp_efuse_mac_get_default (per-chip unique MAC)
#include <esp_log.h>
#include <stdint.h>

// =====================  PER-DEVICE PAIRING IDENTITY  =====================
// By DEFAULT the discriminator and passcode are DERIVED FROM THE CHIP'S UNIQUE
// FACTORY MAC. This means you can flash the SAME firmware image to every unit
// and each one gets a distinct pairing identity automatically — no per-device
// code edits or re-builds. Read each unit's code from the boot serial log
// (the QR URL + manual code are printed via PrintOnboardingCodes, and the
// derived values are also logged in plain text). Rename the accessory in
// Apple Home after adding (e.g. "Bedroom AC").
//
// Manual override (rarely needed): to PIN a specific value on one board,
// #define the macro below to a fixed value before flashing that board.
//   - Passcode: 00000001..99999998, no reserved sequences (see IsValidSetupPIN)
//   - Discriminator: 0..4095
// Leave them undefined (the default) to derive from the MAC.
//
// #define CUSTOM_MATTER_PASSCODE       45822673   // uncomment to pin
// #define CUSTOM_MATTER_DISCRIMINATOR  101        // uncomment to pin
// =========================================================================

class CustomCommissionableDataProvider : public chip::DeviceLayer::CommissionableDataProvider {
public:
  CHIP_ERROR GetSetupDiscriminator(uint16_t &v) override {
    ensureDerived();
    v = mDiscriminator;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupDiscriminator(uint16_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

  CHIP_ERROR GetSpake2pIterationCount(uint32_t &c) override {
    c = chip::Crypto::kSpake2p_Min_PBKDF_Iterations;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &saltBuf) override {
    if (saltBuf.size() < sizeof(kSalt)) return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(saltBuf.data(), kSalt, sizeof(kSalt));
    saltBuf.reduce_size(sizeof(kSalt));
    return CHIP_NO_ERROR;
  }

  // Verifier derived from the (per-device) passcode at runtime.
  CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &verifierBuf, size_t &outLen) override {
    ensureDerived();
    chip::Crypto::Spake2pVerifier verifier;
    CHIP_ERROR err = verifier.Generate(chip::Crypto::kSpake2p_Min_PBKDF_Iterations,
                                       chip::ByteSpan(kSalt), mPasscode);
    if (err != CHIP_NO_ERROR) return err;
    outLen = chip::Crypto::kSpake2p_VerifierSerialized_Length;
    if (verifierBuf.size() < outLen) return CHIP_ERROR_BUFFER_TOO_SMALL;
    err = verifier.Serialize(verifierBuf);
    if (err != CHIP_NO_ERROR) return err;
    verifierBuf.reduce_size(outLen);
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSetupPasscode(uint32_t &p) override {
    ensureDerived();
    p = mPasscode;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupPasscode(uint32_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
  static constexpr uint8_t kSalt[16] = {'A','i','r','C','o','n','d','S',
                                        'p','a','k','e','S','a','l','t'};

  bool     mDerived = false;
  uint16_t mDiscriminator = 0;
  uint32_t mPasscode = 0;

  // FNV-1a 32-bit over a buffer, with a caller-supplied seed so we can produce
  // two independent hashes (one for the discriminator, one for the passcode)
  // from the same MAC. Pure integer math, no float/rand — deterministic.
  static uint32_t fnv1a(const uint8_t *data, size_t len, uint32_t seed) {
    uint32_t h = 2166136261u ^ seed;
    for (size_t i = 0; i < len; i++) {
      h ^= data[i];
      h *= 16777619u;
    }
    return h;
  }

  // A Matter passcode SHALL be 00000001..99999998 excluding a fixed set of
  // trivial values (see PayloadContents::IsValidSetupPIN in CHIP). Mirror that
  // list here so a derived value is always valid.
  static bool isValidPasscode(uint32_t p) {
    if (p == 0 || p > 99999998u) return false;
    switch (p) {
      case 11111111u: case 22222222u: case 33333333u: case 44444444u:
      case 55555555u: case 66666666u: case 77777777u: case 88888888u:
      case 12345678u: case 87654321u:
        return false;
      default:
        return true;
    }
  }

  void ensureDerived() {
    if (mDerived) return;

#if defined(CUSTOM_MATTER_DISCRIMINATOR)
    mDiscriminator = (uint16_t)(CUSTOM_MATTER_DISCRIMINATOR) & 0x0FFF;
#else
    mDiscriminator = 0;  // set from MAC below
#endif
#if defined(CUSTOM_MATTER_PASSCODE)
    mPasscode = (uint32_t)(CUSTOM_MATTER_PASSCODE);
#else
    mPasscode = 0;  // set from MAC below
#endif

    // Derive whatever wasn't pinned from the chip's unique factory MAC.
    uint8_t mac[6] = {0};
    esp_err_t merr = esp_efuse_mac_get_default(mac);
    if (merr != ESP_OK) {
      ESP_LOGE("commission", "esp_efuse_mac_get_default failed (%d); using fallback identity", merr);
    }

#if !defined(CUSTOM_MATTER_DISCRIMINATOR)
    // 12-bit discriminator from a MAC hash.
    mDiscriminator = (uint16_t)(fnv1a(mac, sizeof(mac), 0x0000u) & 0x0FFFu);
#endif

#if !defined(CUSTOM_MATTER_PASSCODE)
    // Passcode from an independent MAC hash, mapped into 1..99999998 and then
    // stepped past the reserved values until valid.
    uint32_t p = (fnv1a(mac, sizeof(mac), 0x9E3779B9u) % 99999998u) + 1u;
    for (int guard = 0; guard < 32 && !isValidPasscode(p); guard++) {
      p = (p % 99999998u) + 1u;
    }
    mPasscode = p;
#endif

    ESP_LOGI("commission",
             "Pairing identity: discriminator=%u  passcode=%08lu  "
             "(MAC %02x:%02x:%02x:%02x:%02x:%02x)",
             (unsigned)mDiscriminator, (unsigned long)mPasscode,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    mDerived = true;
  }
};

inline CustomCommissionableDataProvider g_custom_commissionable_data;

// Register with esp-matter (NOT CHIP's raw SetCommissionableDataProvider, which
// esp-matter overwrites during init). Requires CONFIG_CUSTOM_COMMISSIONABLE_DATA_
// PROVIDER=y so esp-matter installs this provider instead of the test one.
inline void install_custom_commissionable_data() {
  esp_matter::set_custom_commissionable_data_provider(&g_custom_commissionable_data);
}
