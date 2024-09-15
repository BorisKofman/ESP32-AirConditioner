#include "LD2412.h"
#include <Arduino.h>

LD2412::LD2412(HardwareSerial &serial) : serial(serial) {}

void LD2412::begin(HardwareSerial &serial) {
  this->serial = serial;
  Serial.println("LD2412 sensor initialized...");
}

void LD2412::configureSensor() {
  Serial.println("LD2412 sensor configuration complete.");
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
  // Print the current target state in hexadecimal format
  Serial.print("Current Target State: 0x");
  Serial.println(currentTargetState, HEX);

  // Check for valid states (0x01, 0x02, 0x03) and filter invalid states (e.g., 0xF6, 0xF1)
  if (currentTargetState == 0x00) {
    presence = false;  // No presence detected
  } else if (currentTargetState == 0x01 || currentTargetState == 0x02 || currentTargetState == 0x03) {
    presence = true;   // Presence detected
  } else {
    // Invalid state detected, ignore and keep the previous presence state
    #ifdef DEBUG
    Serial.println("Invalid target state detected, ignoring.");
    #endif
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
  // Parse the message header to check if it's a valid frame
  if (isValidFrame(buffer)) {
    char targetState = buffer[8];  // This holds the target state from the message
    handleTargetState(targetState, buffer);
  } else {
    #ifdef DEBUG
    Serial.println("Invalid frame detected. Ignoring the message.");
    #endif
  }
}

bool LD2412::isValidFrame(char *buffer) {
  // Check the frame header (F4 F3 F2 F1) and end of frame (F8 F7 F6 F5)
  return (buffer[0] == 0xF4 && buffer[1] == 0xF3 && buffer[2] == 0xF2 && buffer[3] == 0xF1 && 
          buffer[bufferSize-4] == 0xF8 && buffer[bufferSize-3] == 0xF7 && 
          buffer[bufferSize-2] == 0xF6 && buffer[bufferSize-1] == 0xF5);
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
      #ifdef DEBUG
      Serial.println("No valid target detected.");
      #endif
      break;

    case 0x01:
      // Moving target detected
      movingDistance = (buffer[9] & 0xFF) | ((buffer[10] & 0xFF) << 8);
      stationaryDistance = 0;
      presence = true;
      #ifdef DEBUG
      Serial.println("Moving target detected.");
      Serial.print("Moving distance: ");
      Serial.println(movingDistance);
      #endif
      break;

    case 0x02:
      // Stationary target detected
      stationaryDistance = (buffer[12] & 0xFF) | ((buffer[13] & 0xFF) << 8);
      movingDistance = 0;
      presence = true;
      #ifdef DEBUG
      Serial.println("Stationary target detected.");
      Serial.print("Stationary distance: ");
      Serial.println(stationaryDistance);
      #endif
      break;

    case 0x03:
      // Both moving and stationary targets detected
      movingDistance = (buffer[9] & 0xFF) | ((buffer[10] & 0xFF) << 8);
      stationaryDistance = (buffer[12] & 0xFF) | ((buffer[13] & 0xFF) << 8);
      presence = true;
      #ifdef DEBUG
      Serial.println("Both moving and stationary targets detected.");
      Serial.print("Moving distance: ");
      Serial.println(movingDistance);
      Serial.print("Stationary distance: ");
      Serial.println(stationaryDistance);
      #endif
      break;

    default:
      // Unknown or invalid state, reset distances and ignore the message
      movingDistance = 0;
      stationaryDistance = 0;
      presence = false;
      #ifdef DEBUG
      Serial.println("Invalid or unknown target state detected. Message ignored.");
      #endif
      break;
  }
}