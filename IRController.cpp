#include "IRController.h"

// Constructor
IRController::IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug)
    : irsend(sendPin), irrecv(recvPin, captureBufferSize, timeout, debug), acController(sendPin, false, debug) {
    lastStateValid = false;
}

// Initialize IR sending
void IRController::beginSend() {
    irsend.begin();
    loadLastState();
    loadIdentifiedProtocols();
}

// Initialize IR receiving
void IRController::beginReceive() {
    irrecv.enableIRIn();
}

// Handle incoming IR signals
void IRController::handleIR() {
    decode_results results;
    if (irrecv.decode(&results)) {
        String detectedProtocol = typeToString(results.decode_type);
        Serial.println("Received signal from: " + detectedProtocol);

        if (detectedProtocol != "UNKNOWN" && !detectedProtocol.isEmpty()) {
            if (std::find(identifiedProtocols.begin(), identifiedProtocols.end(), detectedProtocol) == identifiedProtocols.end()) {
                identifiedProtocols.push_back(detectedProtocol);
                saveIdentifiedProtocols();
            }

            String savedProtocol = getProtocol();
            if (savedProtocol.isEmpty() && IRac::isProtocolSupported(results.decode_type)) {
                saveProtocol(detectedProtocol.c_str());
                Serial.println("Saved protocol: " + detectedProtocol);
            } else if (savedProtocol == detectedProtocol) {
                if (IRAcUtils::decodeToState(&results, &lastState, &lastState)) {
                    lastStateValid = true;
                    saveLastState();
                    updateHomeKitFromIR();
                }
            }
        }
        irrecv.resume();
    }
}


// Update HomeKit state from lastState
// Update HomeKit state from lastState
void IRController::updateHomeKitFromIR() {
    if (targetTemp->getVal() != lastState.degrees) {
        targetTemp->setVal(lastState.degrees);
    }

    switch (lastState.mode) {
        case stdAc::opmode_t::kHeat:
            targetState->setVal(1);  // Heat
            break;
        case stdAc::opmode_t::kCool:
            targetState->setVal(2);  // Cool
            break;
        case stdAc::opmode_t::kFan:
            targetState->setVal(3);  // Fan
            break;
        default:
            targetState->setVal(0);  // Off
            break;
    }

    int fanSpeedValue = 0;
    switch (lastState.fanspeed) {
        case stdAc::fanspeed_t::kLow:
            fanSpeedValue = 25;
            break;
        case stdAc::fanspeed_t::kMedium:
            fanSpeedValue = 50;
            break;
        case stdAc::fanspeed_t::kHigh:
            fanSpeedValue = 100;
            break;
        default:
            fanSpeedValue = 0;
            break;
    }
    fanSpeed->setVal(fanSpeedValue);

    swingMode->setVal((lastState.swingv == stdAc::swingv_t::kOff) ? 0 : 1);
}

// Save protocol to preferences
void IRController::saveProtocol(const char *protocol) {
    preferences.begin("IRController", false);
    preferences.putString("protocol", protocol);
    preferences.end();
}

// Get saved protocol from preferences
String IRController::getProtocol() {
    preferences.begin("IRController", true);
    String protocol = preferences.getString("protocol", "");
    preferences.end();
    return protocol;
}

// Save identified protocols
void IRController::saveIdentifiedProtocols() {
    preferences.begin("IRController", false);
    String protocolsString = "";
    for (size_t i = 0; i < identifiedProtocols.size(); i++) {
        protocolsString += identifiedProtocols[i];
        if (i < identifiedProtocols.size() - 1) protocolsString += ",";
    }
    preferences.putString("identifiedProtocols", protocolsString);
    preferences.end();
}

// Load identified protocols
void IRController::loadIdentifiedProtocols() {
    preferences.begin("IRController", true);
    String protocolsString = preferences.getString("identifiedProtocols", "");
    preferences.end();

    identifiedProtocols.clear();
    int start = 0;
    int end = protocolsString.indexOf(',');

    while (end != -1) {
        identifiedProtocols.push_back(protocolsString.substring(start, end));
        start = end + 1;
        end = protocolsString.indexOf(',', start);
    }
    if (start < protocolsString.length()) {
        identifiedProtocols.push_back(protocolsString.substring(start));
    }
}

// Delete identified protocols
void IRController::deleteIdentifiedProtocols() {
    identifiedProtocols.clear();
    preferences.begin("IRController", false);
    preferences.remove("identifiedProtocols");
    preferences.remove("protocol");
    preferences.end();
}

// Save last state
void IRController::saveLastState() {
    preferences.begin("IRController", false);
    preferences.putBytes("lastState", &lastState, sizeof(lastState));
    preferences.end();
}

// Load last state
void IRController::loadLastState() {
    preferences.begin("IRController", true);
    size_t size = preferences.getBytes("lastState", &lastState, sizeof(lastState));
    lastStateValid = (size == sizeof(lastState));
    preferences.end();
}

// Send thermostat command
void IRController::sendThermostatCommand(bool power, int mode, int temp) {
    stdAc::state_t newState = lastState;
    newState.power = power;
    newState.degrees = temp;
  
    // Map HomeKit modes to protocol modes
    switch (mode) {
        case 1:  // Heat
            newState.mode = stdAc::opmode_t::kHeat;
            break;
        case 2:  // Cool
            newState.mode = stdAc::opmode_t::kCool;
            break;
        case 3:  // Auto
            newState.mode = stdAc::opmode_t::kAuto;
            break;
        default:  // Off
            newState.mode = stdAc::opmode_t::kOff;
            break;
    }

    sendCommand(newState);
}

// Send fan command
void IRController::sendFanCommand(int fanSpeed, bool swing) {
    // Use the last saved state as a base
    stdAc::state_t newState = lastState;

    // Map fan speed percentage (0-100) to protocol-specific fan speed levels
    int mappedFanSpeed = (fanSpeed == 0) ? 0   // Off
                        : (fanSpeed <= 33) ? 1 // Low
                        : (fanSpeed <= 66) ? 3 // Medium
                                           : 5; // High
    newState.fanspeed = static_cast<stdAc::fanspeed_t>(mappedFanSpeed);

    // Default to auto swing
    stdAc::swingv_t swingv = stdAc::swingv_t::kAuto;
    stdAc::swingh_t swingh = stdAc::swingh_t::kAuto;

    // Disable swing if the 'swing' flag is true
    if (swing) {
        swingv = stdAc::swingv_t::kOff;
        swingh = stdAc::swingh_t::kOff;
    }

    // Set the new swing values
    newState.swingv = swingv;
    newState.swingh = swingh;

    // Send the updated state to the AC
    sendCommand(newState);
}

void IRController::sendCommand(stdAc::state_t newState) {
    String savedProtocol = getProtocol();

    if (savedProtocol.isEmpty()) {
        Serial.println("No protocol saved. Cannot send command.");
        return;
    }

    irrecv.pause();
    delay(10);

    // Send the command using the AC controller
    if (acController.sendAc(newState, &lastState)) {
        lastState = newState;
        saveLastState();
        Serial.println("IR command sent successfully.");
    } else {
        Serial.println("Failed to send AC command.");
    }

    delay(10);
    irrecv.resume();
}

void IRController::setThermostatCharacteristics(SpanCharacteristic *targetState, SpanCharacteristic *targetTemp) {
    this->targetState = targetState;
    this->targetTemp = targetTemp;
}

void IRController::setLight(bool state) {
    stdAc::state_t newState = lastState;
    newState.light = state ? 1 : 0;  // Assuming the protocol has a light property
    sendCommand(newState);
}