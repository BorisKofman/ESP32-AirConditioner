#ifndef IR_CONTROLLER_H
#define IR_CONTROLLER_H

#include "HomeSpan.h"
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <Preferences.h>

#include <ir_Goodweather.h>
#include <ir_Airton.h>
#include <ir_Amcor.h>
#include <ir_Kelon.h>
#include <ir_Teco.h>

#define GOODWEATHER "GOODWEATHER"
#define AIRTON "AIRTON"
#define AMCOR "AMCOR"
#define KELON "KELON"
#define KELON168 "KELON"
#define TECO "TECO"

class IRController {
private:
    IRsend irsend;
    IRrecv irrecv;
    Preferences preferences;
    String irType;

    IRGoodweatherAc goodweatherAc;
    IRAirtonAc airtonAc;
    IRAmcorAc amcorAc;
    IRKelonAc kelonAc;
    using IRKelonAc168 = IRKelonAc;
    IRTecoAc tecoAc;
    
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
void processACState(ACType& ac); //, SpanCharacteristic* targetState, SpanCharacteristic* coolingTemp

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
    } else if constexpr (std::is_same<ACType, IRKelonAc>::value || std::is_same<ACType, IRKelonAc168>::value) {
        if (direction == 0) {
            ac.setMode(kKelonModeFan);  // Use Kelon-specific fan mode
        }
        ac.setFan(getFanSetting("KELON", fan));

        if constexpr (std::is_same<ACType, IRKelonAc>::value) {
            ac.setTogglePower(power);  // Use ensurePower for Kelon
        } else {
            ac.setPower(power);  // Use setPower for other AC types
        }
    } else if constexpr (std::is_same<ACType, IRTecoAc>::value) {  // Add support for TECO
        if (direction == 0) {
            ac.setMode(kTecoFan);  // Use TECO-specific fan mode
        }
        ac.setFan(getFanSetting("TECO", fan));
        ac.setPower(power);  // Use setPower for TECO

        if constexpr (has_setSwing<ACType>::value) {
            ac.setSwing(swing);
        } else if constexpr (has_setSwingV<ACType>::value) {
            ac.setSwingV(swing);
        }
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
    void configureKelonAc(bool power, int mode, int temp);
    void configureTecoAc(bool power, int mode, int temp);
    int convertToGoodweatherMode(int homeKitMode);
    int convertToAirtonMode(int homeKitMode);
    int convertToAmcorMode(int homeKitMode);
    int convertToKelonMode(int homeKitMode);
    int convertToTecoMode(int homeKitMode);
};

#endif