#ifndef IR_CONTROLLER_H
#define IR_CONTROLLER_H

#include "HomeSpan.h"

#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Goodweather.h>
#include <ir_Airton.h>
#include <Preferences.h>

class IRController {
private:
    IRsend irsend;
    IRrecv irrecv;
    IRGoodweatherAc goodweatherAc;
    IRAirtonAc airtonAc;
    Preferences preferences;
    String irType;

    uint16_t recvPin;  // Add this line
    uint16_t sendPin; // Define the GPIO pin for the IR LED
    uint16_t captureBufferSize;
    uint8_t timeout;
    bool debug;
    // Other member variables...
    
public:
    SpanCharacteristic *active;
    SpanCharacteristic *currentState;
    SpanCharacteristic *coolingTemp;
    SpanCharacteristic *rotationSpeed;
    SpanCharacteristic *targetState;

    IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug);
    void beginsend();
    void beginreceive();
    void handleIR();
    void getIRType();
    void sendCommand(bool power, int mode, int temp, int fan, bool swing);
    void setCharacteristics(SpanCharacteristic *active, SpanCharacteristic *currentState, SpanCharacteristic *coolingTemp, SpanCharacteristic *rotationSpeed);

private:
    void setIRType(String type);
    void decodeIR();
};

#endif // IR_CONTROLLER_H
