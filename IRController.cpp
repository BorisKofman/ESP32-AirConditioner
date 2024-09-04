#include "IRController.h"
#include <cstring> 


const uint8_t kTolerancePercentage = 25; 
const uint16_t kMinUnknownSize = 12; 

IRController::IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug)
    : sendPin(sendPin), recvPin(recvPin), captureBufferSize(captureBufferSize), timeout(timeout), debug(debug), 
      irsend(sendPin), irrecv(recvPin, captureBufferSize, timeout, debug), goodweatherAc(sendPin), airtonAc(sendPin), amcorAc(sendPin) {
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

void IRController::setCharacteristics(SpanCharacteristic *targetState, SpanCharacteristic *coolingTemp) {
    this->currentState = targetState;
    this->coolingTemp = coolingTemp;
}

void IRController::handleIR() {
    decode_results results;
    if (irrecv.decode(&results)) {
        String type = typeToString(results.decode_type);
        if (type == "UNKNOWN") {
            Serial.println("Dropping UNKNOWN");
            irrecv.resume(); 
            return;
        } 
        Serial.print("Received signal from: ");
        Serial.println(type);
        getIRType();  

        if (irType == "UNKNOWN" || irType == "") {
            preferences.begin("ac_ctrl", false);
            preferences.putString("irType", type);
            preferences.end();
            Serial.print("AC control type is configured: ");
            Serial.println(type);
            clearDecodeResults(&results);
            irrecv.resume(); 
            return;
        } else {
            Serial.print("AC control already configured protocol: ");
            Serial.println(irType);

            if (irType == GOODWEATHER && type == GOODWEATHER) {
                goodweatherAc.setRaw(results.value);
                processACState(goodweatherAc, targetState, coolingTemp);
            }
            else if (irType == AIRTON && type == AIRTON) {
                airtonAc.setRaw(results.value);
                processACState(airtonAc, targetState, coolingTemp);
            }
            else if (irType == AMCOR && type == AMCOR) {
                uint8_t raw[sizeof(results.value)];  // Declare the 'raw' array here
                memcpy(raw, &results.value, sizeof(results.value));  // Use 'memcpy' correctly
                amcorAc.setRaw(raw);  // Now 'raw' is in scope and initialized properly
                processACState(amcorAc, targetState, coolingTemp);
            } 
            else {
                Serial.println("Skipping unsupported protocol.");
            }

            clearDecodeResults(&results);
            irrecv.resume(); 
            return; 
        }
    }
}

void IRController::sendCommand(bool power, int mode, int temp) {
    getIRType();
    irrecv.pause();
    delay(10);

    if (irType == "GOODWEATHER") {
        configureGoodweatherAc(power, mode, temp);
        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
    } else if (irType == "AIRTON") {
        configureAirtonAc(power, mode, temp);
        irsend.sendAirton(airtonAc.getRaw(), kAirtonBits);
    } else if (irType == "AMCOR") {
        configureAmcorAc(power, mode, temp);
        irsend.sendAmcor(amcorAc.getRaw(), kAmcorBits);
    } else {
        Serial.println("AC control type is not configured.");
    }

    delay(10);
    irrecv.resume();
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

void IRController::setLight(bool state) {
    getIRType(); 
    if (irType == "GOODWEATHER") {
        irrecv.pause();
        delay(10);

        if (state) {
            goodweatherAc.setLight(1);
        } else {
            goodweatherAc.setLight(0);
        }

        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
        delay(10);  
        irrecv.resume();
    } else {
        Serial.println("Unsupported AC protocol for light control.");
    }
}

void IRController::setFanMode(int power, int fan, bool swing, bool direction) {
    getIRType(); 

    irrecv.pause();
    delay(10);  // Short delay to ensure the receiver is paused

    if (irType == "GOODWEATHER") {
        this->configureFanMode(goodweatherAc, power, fan, swing, direction);
        Serial.println("Sending IR command to set mode to FAN for GOODWEATHER.");
        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
    } else if (irType == "AMCOR") {
        this->configureFanMode(amcorAc, power, fan, swing, direction);
        Serial.println("Sending IR command to set mode to FAN for AMCOR.");
        irsend.sendAmcor(amcorAc.getRaw(), kAmcorBits);
    } else if (irType == "AIRTON") {
        this->configureFanMode(airtonAc, power, fan, swing, direction);
        Serial.println("Sending IR command to set mode to FAN for AIRTON.");
        irsend.sendAirton(airtonAc.getRaw(), kAirtonBits);
    } else {
        Serial.println("Unsupported AC protocol for fan mode.");
    }

    delay(10);
    irrecv.resume();
}

int IRController::getFanSetting(const String& protocol, int fan) {
    if (protocol == "GOODWEATHER") {
        if (fan <= 25) { 
            return kGoodweatherFanLow;
        } else if (fan <= 50) {
            return kGoodweatherFanMed;
        } else if (fan <= 75) {
            return kGoodweatherFanHigh;
        } else {
            return kGoodweatherFanAuto;
        }
    } else if (protocol == "AMCOR") {
        if (fan <= 25) {
            return kAmcorFanMin;
        } else if (fan <= 50) {
            return kAmcorFanMed;
        } else if (fan <= 75) {
            return kAmcorFanMax;
        } else {
            return kAmcorFanAuto;
        }
    } else if (protocol == "AIRTON") {
        if (fan <= 25) {
            return kAirtonFanLow; 
        } else if (fan <= 50) {
            return kAirtonFanMed; 
        } else if (fan <= 75) {
            return kAirtonFanHigh;
        } else {
            return kAirtonFanAuto;
        }
    } else {
        Serial.println("Unknown protocol for fan settings.");
        return -1;
    }
}

//FAN
void IRController::configureGoodweatherAc(bool power, int mode, int temp) {
    goodweatherAc.setPower(power);
    goodweatherAc.setMode(convertToGoodweatherMode(mode));
    goodweatherAc.setTemp(temp);
}

void IRController::configureAirtonAc(bool power, int mode, int temp) {
    airtonAc.setPower(power);
    airtonAc.setMode(convertToAirtonMode(mode));
    airtonAc.setTemp(temp);
}

void IRController::configureAmcorAc(bool power, int mode, int temp) {
    amcorAc.setPower(power);
    amcorAc.setMode(convertToAmcorMode(mode));
    amcorAc.setTemp(temp);
}

int IRController::convertToGoodweatherMode(int homeKitMode) {
    switch (homeKitMode) {
        case 0:  // HomeKit Auto
            return kGoodweatherAuto;
        case 1:  // HomeKit Heat
            return kGoodweatherHeat;
        case 2:  // HomeKit Cool
            return kGoodweatherCool;
        case 3:  // HomeKit Off
            return kGoodweatherFan;
        default:
            return kGoodweatherAuto;
    }
}

int IRController::convertToAirtonMode(int homeKitMode) {
    switch (homeKitMode) {
        case 0:  // HomeKit Auto
            return kAirtonAuto;
        case 1:  // HomeKit Heat
            return kAirtonHeat;
        case 2:  // HomeKit Cool
            return kAirtonCool;
        case 3:  // HomeKit Off
            return kAirtonFan;
        default:
            return kAirtonAuto;
    }
}

int IRController::convertToAmcorMode(int homeKitMode) {
    switch (homeKitMode) {
        case 0:  // HomeKit Auto
            return kAmcorAuto;
        case 1:  // HomeKit Heat
            return kAmcorHeat;
        case 2:  // HomeKit Cool
            return kAmcorCool;
        case 3:  // HomeKit Off
            return kAmcorFan;
        default:
            return kAmcorAuto;
    }
}