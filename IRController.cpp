#include "IRController.h"

const uint8_t kTolerancePercentage = 25; 
const uint16_t kMinUnknownSize = 12; 

// Constructor
IRController::IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug)
    : irsend(sendPin), irrecv(recvPin, captureBufferSize, timeout, debug), acController(sendPin, false, debug) {
    lastStateValid = false;
}

// Helper method for lazy initialization of Preferences
bool IRController::initPreferences(bool readOnly) {
    if (!preferences.begin("IRController", readOnly)) {
        logCharacteristicUpdate("Error", "Failed to initialize Preferences.");
        return false;
    }
    return true;
}

// Initialize IR sending
void IRController::beginSend() {
    logCharacteristicUpdate("Info", "Initializing IR send...");
    irsend.begin();
    logCharacteristicUpdate("Info", "IR sender initialized.");
  
    if (!loadLastState()) {
        logCharacteristicUpdate("Warning", "No valid last state found.");
    } else {
        logCharacteristicUpdate("Info", "Last state loaded successfully.");
    }

    String activeProtocol = getProtocol();
    if (activeProtocol.isEmpty()) {
        logCharacteristicUpdate("Error", "No protocol found. Cannot send commands.");
    } else {
        logCharacteristicUpdate("ActiveProtocol", activeProtocol.c_str());
    }
}

// Initialize IR receiving
void IRController::beginReceive() {
#ifdef DEBUG
    irrecv.enableIRIn();
#else
    irrecv.setTolerance(kTolerancePercentage);
    irrecv.setUnknownThreshold(kMinUnknownSize);
    irrecv.enableIRIn();
#endif
    logCharacteristicUpdate("Info", "IR receiver initialized.");
}

// Handle incoming IR signals
void IRController::handleIR() {
    decode_results results;
    if (irrecv.decode(&results)) {
        String detectedProtocol = typeToString(results.decode_type);
        logCharacteristicUpdate("DetectedProtocol", detectedProtocol.c_str());
        logCharacteristicUpdate("DetectedValue", results.value);
        logCharacteristicUpdate("DetectedCommand", results.command);
        if (detectedProtocol != "UNKNOWN" && !detectedProtocol.isEmpty()) {
            String savedProtocol = getProtocol();

            if (savedProtocol != detectedProtocol) {
                saveProtocol(detectedProtocol.c_str());
                logCharacteristicUpdate("SavedProtocol", detectedProtocol.c_str());
            }

            if (savedProtocol == detectedProtocol) {
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

// Save protocol to preferences
void IRController::saveProtocol(const char *protocol) {
    if (!initPreferences(false)) return;

    if (!isProtocolSaved(protocol)) {
        preferences.putString("protocol", protocol);
        logCharacteristicUpdate("SavedProtocol", protocol);
    }
    preferences.end();
}

// Check if protocol is already saved
bool IRController::isProtocolSaved(const char* protocol) {
    String savedProtocol = getProtocol();
    return savedProtocol == protocol;
}

// Get saved protocol from preferences
String IRController::getProtocol() {
    if (!initPreferences(true)) return "";
    String protocol = preferences.getString("protocol", "");
    preferences.end();
    return protocol;
}

// Update HomeKit state from lastState
void IRController::updateHomeKitFromIR() {
    if (!lastState.power) {
        if (targetState) {
            targetState->setVal(0);  // Off
            logCharacteristicUpdate("TargetState", "Off");
        }
        return;
    }

    if (targetTemp && targetTemp->getVal() != lastState.degrees) {
        targetTemp->setVal(lastState.degrees);
        logCharacteristicUpdate("TargetTemp", lastState.degrees);
    }

    if (targetState) {
        switch (lastState.mode) {
            case stdAc::opmode_t::kHeat:
                targetState->setVal(1);  // Heat
                logCharacteristicUpdate("TargetState", "Heat");
                break;
            case stdAc::opmode_t::kCool:
                targetState->setVal(2);  // Cool
                logCharacteristicUpdate("TargetState", "Cool");
                break;
            case stdAc::opmode_t::kFan:
                targetState->setVal(3);  // Fan
                logCharacteristicUpdate("TargetState", "Fan");
                break;
            default:
                targetState->setVal(0);  // Off
                logCharacteristicUpdate("TargetState", "Off");
                break;
        }
    }

    if (fanRotationSpeed) {
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
        fanRotationSpeed->setVal(fanSpeedValue);
        logCharacteristicUpdate("FanSpeed", fanSpeedValue);
    } else {
        logCharacteristicUpdate("Error", "fanRotationSpeed characteristic is not initialized.");
    }
}

// Save last state
void IRController::saveLastState() {
    if (!initPreferences(false)) return;
    preferences.putBytes("lastState", &lastState, sizeof(lastState));
    preferences.end();
    logCharacteristicUpdate("Info", "Last state saved successfully.");
}

// Load last state
bool IRController::loadLastState() {
    if (!initPreferences(true)) return false;
    size_t size = preferences.getBytes("lastState", &lastState, sizeof(lastState));
    lastStateValid = (size == sizeof(lastState));
    preferences.end();
    return lastStateValid;
}


// Send thermostat command
void IRController::sendThermostatCommand(bool power, int mode, int temp) {
    stdAc::state_t newState = lastState;
    newState.power = power;
    newState.degrees = temp;
    newState.light = true;
    
    switch (mode) {
        case 1:  // Heat
            newState.mode = stdAc::opmode_t::kHeat;
            break;
        case 2:  // Cool
            newState.mode = stdAc::opmode_t::kCool;
            break;
        case 3:  // Auto
            newState.mode = stdAc::opmode_t::kFan;
            break;
        default:  // Off
            newState.mode = stdAc::opmode_t::kOff;
            break;
    }

    sendCommand(newState);
}

// Send fan command
void IRController::sendFanCommand(int fanSpeed, bool swing) {
    stdAc::state_t newState = lastState;

    int mappedFanSpeed = (fanSpeed == 0) ? 1
                        : (fanSpeed <= 25) ? 2
                        : (fanSpeed <= 50) ? 3
                        : (fanSpeed <= 75) ? 4
                        : (fanSpeed == 100) ? 0
                        : 0;
    newState.fanspeed = static_cast<stdAc::fanspeed_t>(mappedFanSpeed);

    stdAc::swingv_t swingv = swing ? stdAc::swingv_t::kOff : stdAc::swingv_t::kAuto;
    newState.swingv = swingv;

    sendCommand(newState);
}

// Send command
void IRController::sendCommand(stdAc::state_t newState) {
    String savedProtocol = getProtocol();
    if (savedProtocol.isEmpty()) {
        logCharacteristicUpdate("Error", "No protocol saved. Cannot send command.");
        return;
    }

    irrecv.pause();
    delay(10);

    if (acController.sendAc(newState, &lastState)) {
        lastState = newState;
        saveLastState();
        logCharacteristicUpdate("Info", "IR command sent successfully.");
    } else {
        logCharacteristicUpdate("Error", "Failed to send AC command.");
    }

    delay(10);
    irrecv.resume();
}

// Set thermostat characteristics
void IRController::setThermostatCharacteristics(SpanCharacteristic *targetState, SpanCharacteristic *targetTemp) {
    this->targetState = targetState;
    this->targetTemp = targetTemp;

    if (this->targetState && this->targetTemp) {
        logCharacteristicUpdate("Info", "Thermostat characteristics initialized.");
    } else {
        logCharacteristicUpdate("Error", "Failed to initialize thermostat characteristics.");
    }
}

// Set fan characteristics
void IRController::setFanCharacteristics(SpanCharacteristic *fanRotationSpeed, SpanCharacteristic *swingMode) {
    this->fanRotationSpeed = fanRotationSpeed;
    this->swingMode = swingMode;

    if (this->fanRotationSpeed && this->swingMode) {
        logCharacteristicUpdate("Info", "Fan characteristics initialized successfully.");
    } else {
        logCharacteristicUpdate("Error", "Failed to initialize fan characteristics.");
    }
}

// Log characteristic updates (string)
void IRController::logCharacteristicUpdate(const char* characteristic, const char* value, const char* level) {
#ifdef DEBUG
    // Log all levels when DEBUG is defined
    Serial.printf("[%s] %s: %s\n", level, characteristic, value);
#else
    // Only log errors when DEBUG is not defined
    if (strcmp(level, "Error") == 0) {
        Serial.printf("[ERROR] %s: %s\n", characteristic, value);
    }
#endif
}

// Log characteristic updates (integer)
void IRController::logCharacteristicUpdate(const char* characteristic, int value, const char* level) {
#ifdef DEBUG
    // Log all levels when DEBUG is defined
    Serial.printf("[%s] %s: %d\n", level, characteristic, value);
#else
    // Only log errors when DEBUG is not defined
    if (strcmp(level, "Error") == 0) {
        Serial.printf("[ERROR] %s: %d\n", characteristic, value);
    }
#endif
}