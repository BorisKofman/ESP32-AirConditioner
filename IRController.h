#ifndef IRCONTROLLER_H_
#define IRCONTROLLER_H_

#include "HomeSpan.h"
#include <Preferences.h>
#include "Config.h"
#include <IRremoteESP8266.h>
#include <vector>
#include <IRac.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

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
    void setFanCharacteristics(SpanCharacteristic *fanRotationSpeed, SpanCharacteristic *swingMode);
    bool isProtocolSaved(const char* protocol); // Declare the helper function

private:
    IRsend irsend;
    IRrecv irrecv;
    IRac acController;
    Preferences preferences;
    stdAc::state_t lastState;
    bool lastStateValid = false;
    SpanCharacteristic *fanRotationSpeed;
    SpanCharacteristic *swingMode;
    SpanCharacteristic *targetTemp;
    SpanCharacteristic *targetState;
    std::vector<String> identifiedProtocols;

    bool initPreferences(bool readOnly);
    void updateHomeKitFromIR();
    void saveLastState();
    bool loadLastState(); 
    void sendCommand(stdAc::state_t newState);
    void logCharacteristicUpdate(const char* characteristic, const char* value, const char* level = "Info");
    void logCharacteristicUpdate(const char* characteristic, int value, const char* level = "Info");
};

#endif  // IRCONTROLLER_H_