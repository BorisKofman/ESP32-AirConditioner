#include "IRController.h"

const uint8_t kTolerancePercentage = kTolerance;


IRController::IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug)
    : irsend(sendPin), irrecv(recvPin, captureBufferSize, timeout, debug), goodweatherAc(sendPin), airtonAc(sendPin) {
}

void IRController::beginsend() {
    irsend.begin();
}

void IRController::beginreceive() {
    irrecv.setTolerance(kTolerancePercentage); // Use kTolerancePercentage here
    Serial.println(recvPin);
    irrecv.enableIRIn();
}

void IRController::beginreceive() {
    irrecv.pouse();
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
        if (!preferences.isKey("irType") || preferences.getString("irType") != type) {
            preferences.putString("irType", type);
        }
        Serial.println(type);
        preferences.end();  // Close NVS storage
        irrecv.resume(); // Receive the next value
    }
}

void IRController::sendCommand(bool power, int mode, int temp, int fan, bool swing) {
 // Convert mode if necessary

    getIRType();  //  // Ensure irType is retrieved before using it

    if (irType == "GOODWEATHER") {
      goodweatherAc.setPower(power);
      if ( mode == 0) { // Auto
          goodweatherAc.setMode(kGoodweatherAuto);
        } else if (mode == 1) { // Heating
          goodweatherAc.setMode(kGoodweatherHeat);  //
        } else if (mode == 2) { // Cooling
          goodweatherAc.setMode(kGoodweatherCool);  // Set mode to cooling
        }
      goodweatherAc.setFan((fan <= 25) ? kGoodweatherFanLow :
        (fan <= 50) ? kGoodweatherFanMed :
        (fan <= 75) ? kGoodweatherFanHigh :
                      kGoodweatherFanAuto);
        goodweatherAc.setTemp(temp);
        goodweatherAc.setSwing(swing);
        irrecv.pause();
        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
        irrecv.resume();
        Serial.println("Sended goodweather");
    } else if (irType == "AIRTON") {
        airtonAc.setPower(power);
        airtonAc.setMode(mode);
        airtonAc.setTemp(temp);
        airtonAc.setFan(fan);
        airtonAc.setSwingV(swing);
        airtonAc.setLight("on");
        irrecv.pause();
        irsend.sendAirton(airtonAc.getRaw(), kAirtonBits);
        irrecv.resume();
        Serial.println("Sended AIRTON");
    } else {
        Serial.println("AC control type is not configured. Configuration needed.");
    }
}

void IRController::setIRType(String type) {
    preferences.begin("ac_ctrl", false);
    preferences.putString("irType", type);
    preferences.end();
}

void IRController::getIRType() {
    preferences.begin("ac_ctrl", false);
    irType = preferences.getString("irType", "");
    preferences.end();
    if (irType == "") {
        Serial.println("AC control type is not configured. Configuration needed.");
    }
}

void IRController::decodeIR() {
    decode_results results;
    if (irrecv.decode(&results)) {
        irType = typeToString(results.decode_type);
        setIRType(irType);
        irrecv.resume(); // Receive the next value
    }

}
