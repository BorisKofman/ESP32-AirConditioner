#include "IRController.h"

// Define constants for remote types
#define GOODWEATHER "GOODWEATHER"
#define AIRTON "AIRTON"

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

void IRController::setCharacteristics(SpanCharacteristic *active, SpanCharacteristic *currentState, SpanCharacteristic *coolingTemp, SpanCharacteristic *rotationSpeed) {
    this->active = active;
    this->currentState = currentState;
    this->coolingTemp = coolingTemp;
    this->rotationSpeed = rotationSpeed;
}

void IRController::handleIR() {

    decode_results results;
    if (irrecv.decode(&results)) {
        getIRType();  // Ensure irType is retrieved before proceeding
        if (irType == "UNKNOWN" || irType == "") {
            preferences.begin("ac_ctrl", false);  // Re-open
            String type = typeToString(results.decode_type);
            preferences.putString("irType", type);
            preferences.end();  // Close NVS storage
            Serial.print(F("AC control type is configured: "));
            Serial.println(irType);
            } 
        else if (irType == GOODWEATHER) {
            // Handle GOODWEATHER remote signals
            goodweatherAc.setRaw(results.value);
            active->setVal(goodweatherAc.getPower());
            currentState->setVal(goodweatherAc.getMode());
            coolingTemp->setVal(goodweatherAc.getTemp());
            } 
        else if (irType == AIRTON) {
            // Handle GOODWEATHER remote signals
            airtonAc.setRaw(results.value);
            active->setVal(airtonAc.getPower());
            currentState->setVal(airtonAc.getMode());
            coolingTemp->setVal(airtonAc.getTemp());
            } 
        irrecv.resume(); // Receive the next value
    }
}

void IRController::sendCommand(bool power, int mode, int temp, int fan, bool swing) {
 // Convert mode if necessary
    getIRType();  //  // Ensure irType is retrieved before using it
    if (irType == "GOODWEATHER") {
      irrecv.pause();
      delay(10);  
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
        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
        irrecv.resume();
        }   
    else if (irType == "AIRTON") {
        irrecv.pause();
        delay(10);  
        airtonAc.setPower(power);
        if ( mode == 0) { // Auto
          airtonAc.setMode(kAirtonAuto);
        } else if (mode == 1) { // Heating
          airtonAc.setMode(kAirtonHeat);  //
        } else if (mode == 2) { // Cooling
          airtonAc.setMode(kAirtonCool);  // Set mode to cooling
        }
        airtonAc.setFan((fan <= 25) ? kAirtonFanLow :
        (fan <= 50) ? kAirtonFanMed :
        (fan <= 75) ? kAirtonFanHigh :
                      kAirtonFanAuto);
        airtonAc.setTemp(temp);
        airtonAc.setSwingV(swing);
        airtonAc.setLight("on");
        irsend.sendAirton(airtonAc.getRaw(), kAirtonBits);
        irrecv.resume();
        } 
    else {
        Serial.println("AC control type is not configured.");
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
}

void IRController::decodeIR() {
    decode_results results;
    if (irrecv.decode(&results)) {
        irType = typeToString(results.decode_type);
        setIRType(irType);
        irrecv.resume(); // Receive the next value
    }
}
