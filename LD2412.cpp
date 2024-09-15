#include "LD2412.h"
#include "config.h"
#include <Arduino.h>

LD2412::LD2412(HardwareSerial &serial) : serial(serial) {}

void LD2412::begin(HardwareSerial &serial) {
  this->serial = serial;
}

void LD2412::configureSensor() {
  // Sensor configuration message
}

void LD2412::read() {
  static char buffer[bufferSize];  
  int bytesRead = 0;

  // Read data from the serial port
  while (serial.available() && bytesRead < bufferSize) {
    buffer[bytesRead] = serial.read();
    bytesRead++;
  }

  // If the buffer is full, process the message
  if (bytesRead == bufferSize) {
    handleMessage(buffer);
  } else {
    bytesRead = 0;  // Reset buffer for the next read
  }
}

bool LD2412::presenceDetected() {
  // Check for valid states (0x01, 0x02, 0x03) and filter invalid states
  if (currentTargetState == 0x00) {
    presence = false;  // No presence detected
  } else if (currentTargetState == 0x01 || currentTargetState == 0x02 || currentTargetState == 0x03) {
    presence = true;   // Presence detected
  }

  return presence;  // Return the current state
}

bool LD2412::stationaryTargetDetected() {
  return stationaryDistance > 0;
}

bool LD2412::movingTargetDetected() {
  return movingDistance > 0;
}

uint16_t LD2412::stationaryTargetDistance() {
  return stationaryDistance;
}

uint16_t LD2412::movingTargetDistance() {
  return movingDistance;
}

void LD2412::handleMessage(char *buffer) {
  // Check if the frame is valid before processing
  if (isValidFrame(buffer)) {
    char targetState = buffer[8];  

    // Handle the extracted target state
    handleTargetState(targetState, buffer);
  }
}

bool LD2412::isValidFrame(char *buffer) {
  // Check the frame header (F4 F3 F2 F1)
  if (buffer[0] != 0xF4 || buffer[1] != 0xF3 || buffer[2] != 0xF2 || buffer[3] != 0xF1) {
    return false;
  }

  // Check for either footer (0xF8 0xF7 0xF6 0xF5) or simple 0x55 footer
  if (!((buffer[bufferSize-4] == 0xF8 && buffer[bufferSize-3] == 0xF7 && 
         buffer[bufferSize-2] == 0xF6 && buffer[bufferSize-1] == 0xF5) || 
        (buffer[bufferSize-1] == 0x55))) {
    return false;
  }

  // Frame is considered valid
  return true;
}

void LD2412::handleTargetState(char targetState, char *buffer) {
  currentTargetState = targetState;  // Store the current target state

  // Only process valid target states and filter out invalid or noise data
  switch (targetState) {
    case 0x00:
      // No target detected, reset distances
      movingDistance = 0;
      stationaryDistance = 0;
      presence = false;
      break;

    case 0x01:
      // Moving target detected
      movingDistance = (buffer[9] & 0xFF) | ((buffer[10] & 0xFF) << 8);
      stationaryDistance = 0;
      presence = true;
      break;

    case 0x02:
      // Stationary target detected
      stationaryDistance = (buffer[12] & 0xFF) | ((buffer[13] & 0xFF) << 8);
      movingDistance = 0;
      presence = true;
      break;

    case 0x03:
      // Both moving and stationary targets detected
      movingDistance = (buffer[9] & 0xFF) | ((buffer[10] & 0xFF) << 8);
      stationaryDistance = (buffer[12] & 0xFF) | ((buffer[13] & 0xFF) << 8);
      presence = true;
      break;

    default:
      // Unknown or invalid state, reset distances and ignore the message
      movingDistance = 0;
      stationaryDistance = 0;
      presence = false;
      break;
  }
}