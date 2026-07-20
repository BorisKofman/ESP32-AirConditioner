// Custom commissioning identity for the native build: a fixed custom passcode
// and discriminator (instead of CHIP's shared test values), with the SPAKE2+
// verifier derived from the passcode at runtime (no offline spake2p tooling).
// Install via install_custom_commissionable_data() BEFORE esp_matter::start().
//
// Ported from the Arduino CustomCommissionableData.h.
#pragma once
#include <platform/CommissionableDataProvider.h>
#include <crypto/CHIPCryptoPAL.h>
#include <lib/support/Span.h>
#include <esp_matter_providers.h>  // esp_matter::set_custom_commissionable_data_provider

// =====================  PER-DEVICE PAIRING IDENTITY  =====================
// Building more than one unit? Give EACH device a UNIQUE discriminator (and
// ideally a unique passcode) before flashing it, then re-flash. The
// discriminator is how the phone tells devices apart during pairing; two
// devices sharing one discriminator will collide.
//   - Passcode: 8 digits, no trivial sequences (not 12345678/00000000, etc.)
//   - Discriminator: 0..4095, unique per device
// The QR URL + manual code are printed to serial on boot (PrintOnboardingCodes).
// Rename the accessory in Apple Home after adding (e.g. "Bedroom AC").
//
//   Device 1: passcode 45822673, discriminator 101   <-- edit for each unit
#define CUSTOM_MATTER_PASSCODE       45822673
#define CUSTOM_MATTER_DISCRIMINATOR  101
// =========================================================================

class CustomCommissionableDataProvider : public chip::DeviceLayer::CommissionableDataProvider {
public:
  CHIP_ERROR GetSetupDiscriminator(uint16_t &v) override {
    v = CUSTOM_MATTER_DISCRIMINATOR;
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

  // Verifier derived from the passcode at runtime.
  CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &verifierBuf, size_t &outLen) override {
    chip::Crypto::Spake2pVerifier verifier;
    CHIP_ERROR err = verifier.Generate(chip::Crypto::kSpake2p_Min_PBKDF_Iterations,
                                       chip::ByteSpan(kSalt), CUSTOM_MATTER_PASSCODE);
    if (err != CHIP_NO_ERROR) return err;
    outLen = chip::Crypto::kSpake2p_VerifierSerialized_Length;
    if (verifierBuf.size() < outLen) return CHIP_ERROR_BUFFER_TOO_SMALL;
    err = verifier.Serialize(verifierBuf);
    if (err != CHIP_NO_ERROR) return err;
    verifierBuf.reduce_size(outLen);
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSetupPasscode(uint32_t &p) override {
    p = CUSTOM_MATTER_PASSCODE;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupPasscode(uint32_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
  static constexpr uint8_t kSalt[16] = {'A','i','r','C','o','n','d','S',
                                        'p','a','k','e','S','a','l','t'};
};

inline CustomCommissionableDataProvider g_custom_commissionable_data;

// Register with esp-matter (NOT CHIP's raw SetCommissionableDataProvider, which
// esp-matter overwrites during init). Requires CONFIG_CUSTOM_COMMISSIONABLE_DATA_
// PROVIDER=y so esp-matter installs this provider instead of the test one.
inline void install_custom_commissionable_data() {
  esp_matter::set_custom_commissionable_data_provider(&g_custom_commissionable_data);
}
