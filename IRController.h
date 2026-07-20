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

// Universal IR AC control: protocol learned from the AC's own remote,
// persisted in NVS, then used for sending (IRremoteESP8266 / IRac)
class IRController {
public:
  // Fired when a state was decoded from the AC remote (for Matter mirroring)
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
  // Reception is ignored briefly after a send (echoes of our own frame)
  unsigned long lastSendMs = 0;

  bool initPreferences(bool readOnly);
  bool isProtocolSaved(const char *protocol);
  void saveLastState();
  bool loadLastState();
  void sendCommand(stdAc::state_t newState);
};

#endif  // IRCONTROLLER_H_
