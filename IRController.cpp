#include "IRController.h"

static const uint8_t kTolerancePercentage = 25;
static const uint16_t kMinUnknownSize = 12;

// IRac's third parameter is use_modulation - the 38kHz carrier every AC
// receiver expects. Always on; without it commands transmit but ACs ignore
// them. (In the HomeSpan version this was accidentally wired to `debug`.)
IRController::IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug)
    : irsend(sendPin), irrecv(recvPin, captureBufferSize, timeout, debug), acController(sendPin, false, true) {
  IRac::initState(&lastState);
  lastStateValid = false;
}

bool IRController::initPreferences(bool readOnly) {
  if (preferences.begin("IRController", readOnly)) {
    return true;
  }
  // Read-only open fails if the namespace was never created (fresh board).
  // Create it with a read-write open, then retry.
  if (readOnly && preferences.begin("IRController", false)) {
    preferences.end();
    if (preferences.begin("IRController", true)) {
      return true;
    }
  }
  Serial.println("[IR] Failed to initialize Preferences");
  return false;
}

void IRController::beginSend() {
  irsend.begin();

  if (!loadLastState()) {
    Serial.println("[IR] No saved AC state yet");
  }

  String activeProtocol = getProtocol();
  if (activeProtocol.isEmpty()) {
    Serial.println("[IR] No protocol learned yet - point the AC remote at the receiver and press a button");
  } else {
    Serial.printf("[IR] Active protocol: %s\n", activeProtocol.c_str());
  }
}

void IRController::beginReceive() {
  irrecv.setTolerance(kTolerancePercentage);
  irrecv.setUnknownThreshold(kMinUnknownSize);
  irrecv.enableIRIn();
  Serial.println("[IR] Receiver initialized");
}

void IRController::handleIR() {
  decode_results results;
  if (!irrecv.decode(&results)) {
    return;
  }

  String detectedProtocol = typeToString(results.decode_type);
  // Log every reception - it's how you know the receiver hardware works
  // even when the protocol isn't recognized.
  Serial.printf("[IR] Received signal: protocol %s, %u bits\n",
                detectedProtocol.c_str(), (unsigned)results.bits);

  // Only AC protocols the IRac engine can send are learnable. Noise can
  // decode as random non-AC protocols (seen: MULTIBRACKETS) and must not
  // poison the saved protocol.
  if (!IRac::isProtocolSupported(results.decode_type)) {
    irrecv.resume();
    return;
  }

  if (detectedProtocol != "UNKNOWN" && !detectedProtocol.isEmpty()) {
    String savedProtocol = getProtocol();

    if (savedProtocol != detectedProtocol) {
      saveProtocol(detectedProtocol.c_str());
      Serial.printf("[IR] Learned protocol: %s\n", detectedProtocol.c_str());
    }
    // Decode the frame into an AC state right away (also on the learning
    // frame - it carries the protocol and current AC settings).
    if (IRAcUtils::decodeToState(&results, &lastState, &lastState)) {
      // The user used the AC's own remote: mirror the change to Matter
      lastStateValid = true;
      saveLastState();
      if (irStateCB) {
        irStateCB(lastState);
      }
    }
  }
  irrecv.resume();
}

void IRController::saveProtocol(const char *protocol) {
  if (!initPreferences(false)) {
    return;
  }
  if (!isProtocolSaved(protocol)) {
    preferences.putString("protocol", protocol);
  }
  preferences.end();
}

bool IRController::isProtocolSaved(const char *protocol) {
  return getProtocol() == protocol;
}

String IRController::getProtocol() {
  if (!initPreferences(true)) {
    return "";
  }
  String protocol = preferences.getString("protocol", "");
  preferences.end();
  return protocol;
}

void IRController::saveLastState() {
  if (!initPreferences(false)) {
    return;
  }
  preferences.putBytes("lastState", &lastState, sizeof(lastState));
  preferences.end();
}

bool IRController::loadLastState() {
  if (!initPreferences(true)) {
    return false;
  }
  size_t size = preferences.getBytes("lastState", &lastState, sizeof(lastState));
  lastStateValid = (size == sizeof(lastState));
  preferences.end();
  return lastStateValid;
}

void IRController::sendThermostatCommand(bool power, int mode, int temp) {
  stdAc::state_t newState = lastState;
  newState.power = power;
  newState.degrees = temp;
  newState.celsius = true;
  newState.light = true;

  switch (mode) {
    case 1:  newState.mode = stdAc::opmode_t::kHeat; break;
    case 2:  newState.mode = stdAc::opmode_t::kCool; break;
    case 3:  newState.mode = AUTO_MODE; break;  // fan-only by default, see Config.h
    default: newState.mode = stdAc::opmode_t::kOff; break;
  }

  sendCommand(newState);
}

void IRController::sendFanCommand(int fanSpeedPercent, bool swing) {
  stdAc::state_t newState = lastState;

  int mappedFanSpeed = (fanSpeedPercent == 0)   ? 1
                       : (fanSpeedPercent <= 33) ? 2
                       : (fanSpeedPercent <= 66) ? 3
                       : (fanSpeedPercent <= 99) ? 4
                                                 : 5;
  newState.fanspeed = static_cast<stdAc::fanspeed_t>(mappedFanSpeed);
  newState.swingv = swing ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;

  sendCommand(newState);
}

void IRController::sendCommand(stdAc::state_t newState) {
  String savedProtocol = getProtocol();
  if (savedProtocol.isEmpty()) {
    Serial.println("[IR] No protocol saved - cannot send. Use the AC remote once to teach it.");
    return;
  }

  // The IRac engine sends whatever newState.protocol says - on a fresh
  // board lastState never had it set, so always stamp the saved protocol.
  decode_type_t proto = strToDecodeType(savedProtocol.c_str());
  if (proto == decode_type_t::UNKNOWN) {
    Serial.printf("[IR] Saved protocol '%s' is not sendable\n", savedProtocol.c_str());
    return;
  }
  newState.protocol = proto;

  // Pause the receiver so it doesn't decode our own transmission
  irrecv.pause();
  delay(10);

  if (acController.sendAc(newState, &lastState)) {
    lastState = newState;
    saveLastState();
    Serial.println("[IR] Command sent");
  } else {
    Serial.println("[IR] Failed to send AC command");
  }

  delay(10);
  irrecv.resume();
}
