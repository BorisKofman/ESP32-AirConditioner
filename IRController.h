#ifndef IRCONTROLLER_H_
#define IRCONTROLLER_H_

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <functional>

// Universal IR AC control (IRremoteESP8266 / IRac). The AC's protocol is
// learned by receiving a signal from its own remote, persisted in NVS, and
// then used for sending. Ported from the HomeSpan version (main branch);
// the HomeKit characteristic coupling is replaced by the onIRState callback.
class IRController {
public:
  // Fired when a valid state was decoded from the AC's own remote,
  // so the Matter side can mirror what the user did physically.
  using IRStateCB = std::function<void(const stdAc::state_t &)>;

  IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug);
  void beginSend();
  void beginReceive();
  void handleIR();  // call from loop()
  void onIRState(IRStateCB cb) {
    irStateCB = cb;
  }

  String getProtocol();
  void saveProtocol(const char *protocol);

  // mode: 0=off, 1=heat, 2=cool, 3=fan-only (AUTO_MODE from Config.h)
  void sendThermostatCommand(bool power, int mode, int temp);
  void sendFanCommand(int fanSpeedPercent, bool swing);

private:
  IRsend irsend;
  IRrecv irrecv;
  IRac acController;
  Preferences preferences;
  stdAc::state_t lastState;
  bool lastStateValid = false;
  IRStateCB irStateCB;

  bool initPreferences(bool readOnly);
  bool isProtocolSaved(const char *protocol);
  void saveLastState();
  bool loadLastState();
  void sendCommand(stdAc::state_t newState);
};

#endif  // IRCONTROLLER_H_
