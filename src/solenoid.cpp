#include "solenoid.h"

namespace {
bool g_is_locked = true;
unsigned long g_unlock_start = 0;
bool g_unlocking = false;
}

void solenoid_init() {
  pinMode(SOLENOID_GATE_PIN, OUTPUT);
  pinMode(SOLENOID_SENSOR_PIN, INPUT_PULLUP);
  digitalWrite(SOLENOID_GATE_PIN, LOW);
  g_is_locked = true;
  Serial.println("Solenoid initialized (locked)");
}

void solenoid_unlock() {
  Serial.println("Solenoid unlocking");
  digitalWrite(SOLENOID_GATE_PIN, HIGH);
  g_is_locked = false;
  g_unlock_start = millis();
  g_unlocking = true;
}

void solenoid_lock() {
  digitalWrite(SOLENOID_GATE_PIN, LOW);
  g_is_locked = true;
  Serial.println("Solenoid locked");
}

bool solenoid_is_locked() {
  return g_is_locked;
}

bool solenoid_sensor_locked() {
  return digitalRead(SOLENOID_SENSOR_PIN) == LOW;
}

void solenoid_loop() {
  if (!g_unlocking) {
    return;
  }

  if (millis() - g_unlock_start >= SOLENOID_UNLOCK_MS) {
    digitalWrite(SOLENOID_GATE_PIN, LOW);
    g_is_locked = true;
    g_unlocking = false;
    Serial.println("Solenoid locked (auto)");
  }
}
