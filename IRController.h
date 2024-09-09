#ifndef IR_CONTROLLER_H
#define IR_CONTROLLER_H

#include "HomeSpan.h"
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Goodweather.h>
#include <ir_Airton.h>
#include <ir_Amcor.h>
#include <Preferences.h>

#define GOODWEATHER "GOODWEATHER"
#define AIRTON "AIRTON"
#define AMCOR "AMCOR"

class IRController {
private:
    IRsend irsend;
    IRrecv irrecv;
    IRAirtonAc airtonAc;
    IRGoodweatherAc goodweatherAc;
    IRAmcorAc amcorAc;
    Preferences preferences;
    String irType;

    uint16_t recvPin;
    uint16_t sendPin;
    uint16_t captureBufferSize;
    uint8_t timeout;
    bool debug;

    template<typename T, typename = void>
    struct has_setSwing : std::false_type {};

    template<typename T>
    struct has_setSwing<T, std::void_t<decltype(std::declval<T>().setSwing(std::declval<bool>()))>> : std::true_type {};

    template<typename T, typename = void>
    struct has_setSwingV : std::false_type {};

    template<typename T>
    struct has_setSwingV<T, std::void_t<decltype(std::declval<T>().setSwingV(std::declval<bool>()))>> : std::true_type {};

public:
    SpanCharacteristic *currentState;
    SpanCharacteristic *targetState;
    SpanCharacteristic *coolingTemp;
    
    IRController(uint16_t sendPin, uint16_t recvPin, uint16_t captureBufferSize, uint8_t timeout, bool debug);
    void beginsend();
    void beginreceive();
    void handleIR();
    void getIRType();
    void clearDecodeResults(decode_results *results);
    void sendCommand(bool power, int mode, int temp);
    void setCharacteristics(SpanCharacteristic *targetState, SpanCharacteristic *coolingTemp); 
    void setLight(bool state);
    void setFanMode(int power, int fan, bool swing, bool direction);
    int getFanSetting(const String& protocol, int fan);
    
    template<typename ACType>
    void processACState(ACType& ac, SpanCharacteristic* targetState, SpanCharacteristic* coolingTemp) {
        targetState->setVal(ac.getPower());
        Serial.print(ac.getPower());
        if (ac.getPower() != 0) {
            int mode = ac.getMode();
            Serial.print(mode);
            switch (mode) {
                case 0:  // Remote auto, HomeKit auto
                    targetState->setVal(0);
                    break;
                case 4:  // Remote heating, HomeKit heating
                    targetState->setVal(1);
                    break;
                case 1:  // Remote cooling, HomeKit cooling
                    targetState->setVal(2);
                    break;
                default:
                    // Handle other cases or do nothing
                    break;
            }
            coolingTemp->setVal(ac.getTemp());
        }
    }

  template<typename ACType>
void configureFanMode(ACType& ac, int power, int fan, bool swing, bool direction) {
    if constexpr (std::is_same<ACType, IRGoodweatherAc>::value) {
        if (direction == 0) {
            ac.setMode(kGoodweatherFan);  // Use Goodweather-specific fan mode
        }
        ac.setFan(getFanSetting("GOODWEATHER", fan));
    } else if constexpr (std::is_same<ACType, IRAirtonAc>::value) {
        if (direction == 0) {
            ac.setMode(kAirtonFan);  // Use Airton-specific fan mode
        }
        ac.setFan(getFanSetting("AIRTON", fan));
    } else if constexpr (std::is_same<ACType, IRAmcorAc>::value) {
        if (direction == 0) {
            ac.setMode(kAmcorFan);  // Use Amcor-specific fan mode
        }
        ac.setFan(getFanSetting("AMCOR", fan));
    }

    if (power != 0) {
        ac.setPower(power);
    }

    if constexpr (has_setSwing<ACType>::value) {
        ac.setSwing(swing);
    } else if constexpr (has_setSwingV<ACType>::value) {
        ac.setSwingV(swing);
    }
}

    void configureGoodweatherAc(bool power, int mode, int temp);
    void configureAirtonAc(bool power, int mode, int temp);
    void configureAmcorAc(bool power, int mode, int temp);
    int convertToGoodweatherMode(int homeKitMode);
    int convertToAirtonMode(int homeKitMode);
    int convertToAmcorMode(int homeKitMode);
};

#endif