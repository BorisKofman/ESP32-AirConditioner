#ifndef RADAR_ACCESSORY_H
#define RADAR_ACCESSORY_H

#include "Config.h" 


#ifdef USE_LD2450
#include "LD2450.h" 
#elif defined(USE_LD2412)
#include "LD2412.h"
#else
#include <ld2410.h>
#endif

#include <HardwareSerial.h>

class RadarAccessory : public Service::OccupancySensor {
  private:
    SpanCharacteristic *occupancy;
    
    #ifdef USE_LD2450
    LD2450 *radar;  
    #endif
    #ifdef USE_LD2412
    LD2412 *radar;
    #endif
    #ifdef USE_LD2410
    ld2410 *radar;
    #endif

    int minRange;
    int maxRange;
    bool presence = false;
    unsigned long previousMillis = 0; 
    const long interval = 1000; 

  public:
    RadarAccessory(
      #ifdef USE_LD2450
      LD2450 *radarSensor,
      #endif 
      #ifdef USE_LD2412
      LD2412 *radarSensor, 
      #endif
      #ifdef USE_LD2410
      ld2410 *radarSensor, 
      #endif
      int minRange, int maxRange) 
      : Service::OccupancySensor(), 
        minRange(minRange), maxRange(maxRange) {
      #ifdef USE_LD2450
      radar = radarSensor;
      #endif
      #ifdef USE_LD2412
      radar = radarSensor;
      #endif
      #ifdef USE_LD2410
      radar = radarSensor;
      #endif
      occupancy = new Characteristic::OccupancyDetected(0, true);
    }

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis; 
      
#ifdef USE_LD2450
    if (radar->read() > 0) {
        bool anyTargetsDetected = false;

        for (uint16_t i = 0; i < radar->getSensorSupportedTargetCount(); i++) {
            LD2450::RadarTarget target = radar->getTarget(i);
            if (target.valid) {
                anyTargetsDetected = true;

                if (target.distance >= minRange && target.distance <= maxRange) {
                    presence = true;
                    break;
                }
            }
        }

        if (!anyTargetsDetected) {
            Serial.println("No targets detected.");
        }
    }

#elif defined(USE_LD2410) || defined(USE_LD2412)
    if (radar->presenceDetected()) {

      #ifdef DEBUG
        Serial.println("Presence detected.");
      #endif

      // Custom condition when "DISTANCE" is not defined
      #ifndef DISTANCE
        presence = true;  // Skip range checks, just set presence to true
        #ifdef DEBUG
          Serial.println("Presence detected, no range checking.");
        #endif
      #else
        // If "DISTANCE" is defined, use the range checking logic
        int stationaryDist = radar->stationaryTargetDistance();
        int movingDist = radar->movingTargetDistance();

        #ifdef DEBUG
          Serial.print("Stationary target distance: ");
          Serial.println(stationaryDist);

          Serial.print("Moving target distance: ");
          Serial.println(movingDist);
        #endif

        // Perform the range checks when DISTANCE is defined
        if ((stationaryDist >= minRange && stationaryDist <= maxRange) || 
            (movingDist >= minRange && movingDist <= maxRange)) {
            presence = true;
            #ifdef DEBUG
              Serial.println("Presence within range.");
            #endif
        } else {
            #ifdef DEBUG
              Serial.println("Presence detected but out of range.");
            #endif
        }
      #endif  // End of DISTANCE check

    } else {
      #ifdef DEBUG
        Serial.println("No presence detected.");
      #endif
    }
#endif
      // Set occupancy value based on the presence
    occupancy->setVal(presence);
    }
  }
};

#endif