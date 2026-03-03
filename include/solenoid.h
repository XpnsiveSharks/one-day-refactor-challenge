#pragma once
#include <Arduino.h>

#define SOLENOID_GATE_PIN    18
#define SOLENOID_SENSOR_PIN  16
#define SOLENOID_UNLOCK_MS   1000

void solenoid_init();
void solenoid_unlock();
void solenoid_lock();
bool solenoid_is_locked();      // from software state
bool solenoid_sensor_locked();  // from hardware sensor pin
void solenoid_loop();
