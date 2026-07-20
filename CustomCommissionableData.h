#pragma once
// Custom pairing identity (Config.h) instead of the shared CHIP test values.
// Apply AFTER Matter.begin() - server startup installs its provider over ours.

#include "Config.h"
#include <Matter.h>
#include <app/server/Server.h>
#include <platform/CommissionableDataProvider.h>
#include <crypto/CHIPCryptoPAL.h>
#include <setup_payload/SetupPayload.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>

// Must match the VID/PID compiled into the core's Basic Information cluster
static constexpr uint16_t kMatterVendorId = 0xFFF1;
static constexpr uint16_t kMatterProductId = 0x8000;

class CustomCommissionableDataProvider : public chip::DeviceLayer::CommissionableDataProvider {
public:
  CHIP_ERROR GetSetupDiscriminator(uint16_t &setupDiscriminator) override {
    setupDiscriminator = MATTER_DISCRIMINATOR;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR SetSetupDiscriminator(uint16_t) override {
    return CHIP_ERROR_NOT_IMPLEMENTED;
  }

  CHIP_ERROR GetSpake2pIterationCount(uint32_t &iterationCount) override {
    iterationCount = chip::Crypto::kSpake2p_Min_PBKDF_Iterations;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &saltBuf) override {
    if (saltBuf.size() < sizeof(kSalt)) {
      return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(saltBuf.data(), kSalt, sizeof(kSalt));
    saltBuf.reduce_size(sizeof(kSalt));
    return CHIP_NO_ERROR;
  }

  // Verifier derived from the passcode at runtime - no offline spake2p tooling
  CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &verifierBuf, size_t &outVerifierLen) override {
    chip::Crypto::Spake2pVerifier verifier;
    CHIP_ERROR err = verifier.Generate(chip::Crypto::kSpake2p_Min_PBKDF_Iterations,
                                       chip::ByteSpan(kSalt), MATTER_PASSCODE);
    if (err != CHIP_NO_ERROR) {
      return err;
    }
    outVerifierLen = chip::Crypto::kSpake2p_VerifierSerialized_Length;
    if (verifierBuf.size() < outVerifierLen) {
      return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    err = verifier.Serialize(verifierBuf);
    if (err != CHIP_NO_ERROR) {
      return err;
    }
    verifierBuf.reduce_size(outVerifierLen);
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSetupPasscode(uint32_t &setupPasscode) override {
    setupPasscode = MATTER_PASSCODE;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR SetSetupPasscode(uint32_t) override {
    return CHIP_ERROR_NOT_IMPLEMENTED;
  }

private:
  static constexpr uint8_t kSalt[16] = {'A', 'i', 'r', 'C', 'o', 'n', 'd', 'S',
                                        'p', 'a', 'k', 'e', 'S', 'a', 'l', 't'};
};

inline CustomCommissionableDataProvider customCommissionableData;

// Install our provider and reopen the commissioning window (if open) so it
// advertises our identity instead of the test values captured at startup
inline void applyCustomCommissionableData() {
  chip::DeviceLayer::SetCommissionableDataProvider(&customCommissionableData);

  chip::DeviceLayer::PlatformMgr().LockChipStack();
  auto &windowMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
  if (windowMgr.IsCommissioningWindowOpen()) {
    windowMgr.CloseCommissioningWindow();
    windowMgr.OpenBasicCommissioningWindow();
  }
  chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

inline chip::SetupPayload buildSetupPayload() {
  chip::SetupPayload payload;
  payload.version = 0;
  payload.vendorID = kMatterVendorId;
  payload.productID = kMatterProductId;
  payload.commissioningFlow = chip::CommissioningFlow::kStandard;
  payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlags(
      chip::RendezvousInformationFlag::kBLE, chip::RendezvousInformationFlag::kOnNetwork));
  payload.discriminator.SetLongValue(MATTER_DISCRIMINATOR);
  payload.setUpPINCode = MATTER_PASSCODE;
  return payload;
}

inline String getCustomManualPairingCode() {
  std::string code;
  chip::SetupPayload payload = buildSetupPayload();
  if (chip::ManualSetupPayloadGenerator(payload).payloadDecimalStringRepresentation(code) != CHIP_NO_ERROR) {
    return String("<manual code generation failed>");
  }
  return String(code.c_str());
}

inline String getCustomQRCodeUrl() {
  std::string qr;
  chip::SetupPayload payload = buildSetupPayload();
  if (chip::QRCodeSetupPayloadGenerator(payload).payloadBase38Representation(qr) != CHIP_NO_ERROR) {
    return String("<QR code generation failed>");
  }
  return String("https://project-chip.github.io/connectedhomeip/qrcode.html?data=") + qr.c_str();
}
