#include "IRController.h"

IRController::IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug)
    : irsend(sendPin), irrecv(recvPin, captureBufferSize, timeout, debug), goodweatherAc(sendPin), airtonAc(sendPin) {
}

void IRController::begin() {
    irsend.begin();
    irrecv.setTolerance(kTolerancePercentage); // Use kTolerancePercentage here
    irrecv.enableIRIn();
    irType = getIRType();
}

void IRController::setCharacteristics(SpanCharacteristic *active, SpanCharacteristic *currentState, SpanCharacteristic *coolingTemp, SpanCharacteristic *rotationSpeed) {
    this->active = active;
    this->currentState = currentState;
    this->coolingTemp = coolingTemp;
    this->rotationSpeed = rotationSpeed;
}

void IRController::handleIR() {
    decode_results results;
    if (irrecv.decode(&results)) {
        preferences.begin("ac_ctrl", false);  // Re-open NVS storage with namespace "ac_ctrl"
        String type = typeToString(results.decode_type);

        if (!preferences.isKey("irType") && preferences.putString("irType", type)) {
            preferences.end();  // Close NVS storage
            irrecv.resume(); // Receive the next value
        } else {
            preferences.end();  // Close NVS storage
            int fanSpeed;  // Declare fanSpeed here
            if (type == "GOODWEATHER") {
                goodweatherAc.setRaw(results.value);
                active->setVal(goodweatherAc.getPower());
                currentState->setVal(goodweatherAc.getMode());
                coolingTemp->setVal(goodweatherAc.getTemp());
                fanSpeed = goodweatherAc.getFan();
            } else if (type == "AIRTON") {
                airtonAc.setRaw(results.value);
                active->setVal(airtonAc.getPower());
                currentState->setVal(airtonAc.getMode());
                coolingTemp->setVal(airtonAc.getTemp());
                fanSpeed = airtonAc.getFan();
            }
            int rotationValue;
            if (fanSpeed == 0) {
                rotationValue = 100;
            } else if (fanSpeed == 1) {
                rotationValue = 75;
            } else if (fanSpeed == 2) {
                rotationValue = 50;
            } else if (fanSpeed == 3) {
                rotationValue = 25;
            }
            rotationSpeed->setVal(rotationValue);
            irrecv.resume(); // Receive the next value
        }
    }
}

void IRController::sendCommand(bool power, int mode, int temp, int fan, bool swing) {
    if (irType == "GOODWEATHER") {
        goodweatherAc.setPower(power);
        goodweatherAc.setMode(mode);
        goodweatherAc.setTemp(temp);
        goodweatherAc.setFan(fan);
        goodweatherAc.setSwing(swing);
        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
    } else if (irType == "AIRTON") {
        airtonAc.setPower(power);
        airtonAc.setMode(mode);
        airtonAc.setTemp(temp);
        airtonAc.setFan(fan);
        airtonAc.setSwingV(swing);
        airtonAc.getLight(on)
        irsend.sendAirton(airtonAc.getRaw(), kAirtonBits);
    } else {
        Serial.println("AC control type is not configured. Configuration needed.");
    }
}

void IRController::setIRType(String type) {
    preferences.begin("ac_ctrl", false);
    preferences.putString("irType", type);
    preferences.end();
}

String IRController::getIRType() {
    preferences.begin("ac_ctrl", false);
    String type = preferences.getString("irType", "");
    preferences.end();
    if (type == "") {
        Serial.println("AC control type is not configured. Configuration needed.");
    }
    return type;
}

void IRController::decodeIR() {
    decode_results results;
    if (irrecv.decode(&results)) {
        irType = typeToString(results.decode_type);
        setIRType(irType);
        irrecv.resume(); // Receive the next value
    }
}
