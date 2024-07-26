#include "IRController.h"

// Define constants for remote types
#define GOODWEATHER "GOODWEATHER"
#define AIRTON "AIRTON"

const uint8_t kTolerancePercentage = kTolerance;


IRController::IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug)
    : sendPin(sendPin), recvPin(recvPin), captureBufferSize(captureBufferSize), timeout(timeout), debug(debug), 
      irsend(sendPin), irrecv(recvPin, captureBufferSize, timeout, debug), goodweatherAc(sendPin), airtonAc(sendPin) {
}


void IRController::beginsend() {
    irsend.enableIROut(38000, 50); 
    irsend.begin();
    Serial.println("send pin");
    Serial.print(sendPin);

}

void IRController::beginreceive() {
    irrecv.setTolerance(kTolerancePercentage);
    Serial.println("receiver pin");
    Serial.print(recvPin);
    Serial.print(captureBufferSize);
    Serial.print(timeout);
    Serial.print(debug);
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
        String type = typeToString(results.decode_type);
        Serial.println(irType);
        if (irType == "UNKNOWN" || irType == "") {
            if (type == "UNKNOWN" ) {
              Serial.print(F("AC control type not configured: UNKNOWN "));
              return;  // Exit the function
            } else {
              preferences.begin("ac_ctrl", false);  // Re-open
              preferences.putString("irType", type);
              preferences.end();  // Close NVS storage
              Serial.print(F("AC control type is configured: "));
              Serial.println(type);
            }
            Serial.println(type);  
          }
        else if (irType == GOODWEATHER) {
            goodweatherAc.setRaw(results.value);
            active->setVal(goodweatherAc.getPower());
            
            if (goodweatherAc.getPower() != 0) {
                currentState->setVal(goodweatherAc.getMode());
                coolingTemp->setVal(goodweatherAc.getTemp());
            }
        }
        else if (irType == AIRTON) {
            airtonAc.setRaw(results.value);
            active->setVal(airtonAc.getPower());
            
            if (airtonAc.getPower() != 0) {
                currentState->setVal(airtonAc.getMode());
                coolingTemp->setVal(airtonAc.getTemp());
            }
        }
        irrecv.resume(); 
    }
}

void IRController::sendCommand(bool power, int mode, int temp, int fan, bool swing) {
 // Convert mode if necessary
    getIRType();  //  // Ensure irType is retrieved before using it
    if (irType == "GOODWEATHER") {
      irrecv.disableIRIn();
      delay(10);  
      goodweatherAc.setPower(power);
      if (power == 0) {
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
        }
        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
        delay(10);  // Short delay to ensure the command is sent
        irrecv.enableIRIn();  // Resume IR receiver
        }   
    else if (irType == "AIRTON") {
        irrecv.pause();
        delay(20);  
        airtonAc.setPower(power);
        if (power == 0) {
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
        }
        irsend.sendAirton(airtonAc.getRaw(), kAirtonBits);
        delay(25);  // Short delay to ensure the command is sent 
        irrecv.resume();
        } 
    else {
        Serial.println("AC control type is not configured.");
    }
  Serial.println("Signal.");
}

void IRController::getIRType() {
    preferences.begin("ac_ctrl", true);
    irType = preferences.getString("irType", "");
    preferences.end();
}
