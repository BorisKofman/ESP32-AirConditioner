#include "IRController.h"

// Define constants for remote types
#define GOODWEATHER "GOODWEATHER"
#define AIRTON "AIRTON"

const uint8_t kTolerancePercentage = kTolerance;
const uint16_t kMinUnknownSize = 12;


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
    irrecv.setUnknownThreshold(kMinUnknownSize);
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
        String type = typeToString(results.decode_type);
        if (type == "UNKNOWN") {
          Serial.println("Dropping UNKNOWN"); //noise 
          irrecv.resume(); 
          return;
        } 
        Serial.print("Received signal from: ");
        Serial.println(type);
        getIRType();  // Ensure irType is retrieved before proceeding
        if (irType == "UNKNOWN" || irType == "") {
          preferences.begin("ac_ctrl", false);  // Re-open
          preferences.putString("irType", type);
          preferences.end();  // Close NVS storage
          Serial.print("AC control type is configured: ");
          Serial.println(type);
          clearDecodeResults(&results);
          irrecv.resume(); 
          return;
        } else {
          Serial.print("AC control Alrady configured protocol: ");
          Serial.println(irType);
          if (irType == GOODWEATHER && type == GOODWEATHER) {
            goodweatherAc.setRaw(results.value);
            active->setVal(goodweatherAc.getPower());
            
            if (goodweatherAc.getPower() != 0) {
                int mode = goodweatherAc.getMode();
  
                switch (mode) {
                  case 0: //remote auto homekit auto
                      currentState->setVal(0);
                      break;
                  case 4; //remote heating homekit heating 
                      currentState->setVal(1);
                      break;
                  case 1; //remote cooling homekit cooling 
                      currentState->setVal(2);
                      break;

                  default:
                      // Handle other cases or do nothing
                      break;
                }
                coolingTemp->setVal(goodweatherAc.getTemp());
            }
          }
          else if (irType == AIRTON && type == AIRTON) {
              airtonAc.setRaw(results.value);
              active->setVal(airtonAc.getPower());
              
              if (airtonAc.getPower() != 0) {
                int mode = airtonAc.getMode();

                switch (mode) {
                  case 0: //remote auto homekit auto
                      currentState->setVal(0);
                      break;
                  case 4; //remote heating homekit heating 
                      currentState->setVal(1);
                      break;
                  case 1; //remote cooling homekit cooling 
                      currentState->setVal(2);
                      break;
                  default:
                      // Handle other cases or do nothing
                      break;
                }
                coolingTemp->setVal(airtonAc.getTemp());
              }
          }
          else {
            Serial.print("Skiping");
            clearDecodeResults(&results);
            irrecv.resume(); 
            return; 
          }
        clearDecodeResults(&results);
        irrecv.resume(); 
        return; 
        }
      irrecv.resume(); 
      return; 
    }
}

void IRController::sendCommand(bool power, int mode, int temp, int fan, bool swing) {
 // Convert mode if necessary
    getIRType();  //  // Ensure irType is retrieved before using it
    if (irType == "GOODWEATHER") {
      irrecv.pause();
      delay(10);  
      goodweatherAc.setPower(power);
      if ( power != 0) {
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
        irrecv.resume();  // Resume IR receiver
        return;
        }   
    else if (irType == "AIRTON") {
        irrecv.pause();
        delay(10);  
        airtonAc.setPower(power);
        if (power != 0) {
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
        delay(10);  // Short delay to ensure the command is sent 
        irrecv.resume();
        return;
        } 
    else {
        Serial.println("AC control type is not configured.");
        return;
    }
}

void IRController::getIRType() {
    preferences.begin("ac_ctrl", true);
    irType = preferences.getString("irType", "");
    preferences.end();
}

void IRController::clearDecodeResults(decode_results *results) {
  results->decode_type = UNKNOWN;
  results->value = 0;
  results->address = 0;
  results->command = 0;
  results->bits = 0;
  results->rawlen = 0;
  results->overflow = false;
  results->repeat = false;
  memset(results->state, 0, sizeof(results->state));
}
