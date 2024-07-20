#ifndef IR_CONTROLLER_H
#define IR_CONTROLLER_H

#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Goodweather.h>
#include <ir_Airton.h>
#include <Preferences.h>
#include "HomeSpan.h"

class IRController {
private:
    IRsend irsend;
    IRrecv irrecv;
    IRGoodweatherAc goodweatherAc;
    IRAirtonAc airtonAc;
    Preferences preferences;

    String irType;

    SpanCharacteristic *active;
    SpanCharacteristic *currentState;
    SpanCharacteristic *coolingTemp;
    SpanCharacteristic *rotationSpeed;

    const uint8_t kTolerancePercentage = 25; // Add this constant

public:
    IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug = false);
    void begin();
    void handleIR();
    void sendCommand(bool power, int mode, int temp, int fan, bool swing);
    void setCharacteristics(SpanCharacteristic *active, SpanCharacteristic *currentState, SpanCharacteristic *coolingTemp, SpanCharacteristic *rotationSpeed);

private:
    void setIRType(String type);
    String getIRType();
    void decodeIR();
};

#endif // IR_CONTROLLER_H
