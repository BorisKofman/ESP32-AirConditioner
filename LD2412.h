#ifndef LD2412_H
#define LD2412_H

#include <HardwareSerial.h>

class LD2412 {
public:
  LD2412(HardwareSerial &serial);
  void begin(HardwareSerial &serial);
  void configureSensor();
  void read();
  bool presenceDetected();
  bool stationaryTargetDetected();
  bool movingTargetDetected();
  uint16_t stationaryTargetDistance();
  uint16_t movingTargetDistance();
  bool isValidFrame(char *buffer);


private:
  HardwareSerial &serial;
  
  int movingDistance;
  int stationaryDistance;
  char currentTargetState;
  bool presence = false; 
  static const int bufferSize = 16; 
  bool debug = false;                

  void handleMessage(char *buffer);
  void handleTargetState(char targetState, char *buffer);
};

#endif