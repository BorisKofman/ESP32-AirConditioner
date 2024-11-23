#ifndef IRCONTROLLER_H_
#define IRCONTROLLER_H_

#include <vector>
#include <IRac.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "HomeSpan.h"
#include <Preferences.h>
#include <IRremoteESP8266.h>

class IRController {
public:
    IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug);
    void beginSend();
    void beginReceive();
    void handleIR();
    void deleteIdentifiedProtocols();
    void saveIdentifiedProtocols();
    void loadIdentifiedProtocols();
    std::vector<String> getIdentifiedProtocols();
    void saveProtocol(const char *protocol);
    void setProtocol(const String &protocol);
    String getProtocol();
    void sendThermostatCommand(bool power, int mode, int temp);
    void sendFanCommand(int fanSpeed, bool swing);
    void setThermostatCharacteristics(SpanCharacteristic *targetState, SpanCharacteristic *targetTemp);
    void setFanCharacteristics(SpanCharacteristic *fanSpeed, SpanCharacteristic *swingMode);
    void setLight(bool state);
    
private:
    IRsend irsend;
    IRrecv irrecv;
    IRac acController;
    Preferences preferences;
    stdAc::state_t lastState;
    bool lastStateValid = false;
    SpanCharacteristic *fanSpeed;
    SpanCharacteristic *swingMode;
    SpanCharacteristic *targetTemp;
    SpanCharacteristic *targetState;
    std::vector<String> identifiedProtocols;

    void updateHomeKitFromIR();
    void saveLastState();
    void loadLastState();
    void sendCommand(stdAc::state_t newState);
};

#endif  // IRCONTROLLER_H_