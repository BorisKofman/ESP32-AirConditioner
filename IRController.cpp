#include "IRController.h"

// Define constants used in the IRController
const uint8_t kTolerancePercentage = 25;  // Example tolerance percentage value
const uint16_t kMinUnknownSize = 12;  // Example unknown size threshold

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
            Serial.println("Dropping UNKNOWN");
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
            Serial.print("AC control already configured protocol: ");
            Serial.println(irType);
            // Handle specific IR types like GOODWEATHER and AIRTON
            // Based on irType and results decode data
        }
        clearDecodeResults(&results);
        irrecv.resume();
        return;
    }
    // Method to handle received IR signals and process them.
}

void IRController::sendCommand(bool power, int mode, int temp, int fan, bool swing) {
    getIRType();  // Ensure irType is retrieved before using it
    if (irType == "GOODWEATHER") {
        irrecv.pause();
        delay(10);
        goodweatherAc.setPower(power);
        if (power != 0) {
            if (mode == 0) {
                goodweatherAc.setMode(kGoodweatherAuto);
            } else if (mode == 1) {
                goodweatherAc.setMode(kGoodweatherHeat);
            } else if (mode == 2) {
                goodweatherAc.setMode(kGoodweatherCool);
            }
            goodweatherAc.setTemp(temp);
            goodweatherAc.setSwing(swing);
            goodweatherAc.setFan((fan <= 25) ? kGoodweatherFanLow :
                                 (fan <= 50) ? kGoodweatherFanMed :
                                 (fan <= 75) ? kGoodweatherFanHigh :
                                               kGoodweatherFanAuto);
        }
        irsend.sendGoodweather(goodweatherAc.getRaw(), kGoodweatherBits);
        delay(10);
        irrecv.resume();
    } else if (irType == "AIRTON") {
        // Similar handling for AIRTON protocol
    } else {
        Serial.println("AC control type is not configured.");
    }
    // Method to send IR command based on power, mode, temp, fan, and swing parameters.
}

void IRController::getIRType() {
    preferences.begin("ac_ctrl", true);
    irType = preferences.getString("irType", "");
    preferences.end();
    // Method to retrieve the stored IR type from non-volatile storage.
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
    // Method to clear IR decoding results.
}