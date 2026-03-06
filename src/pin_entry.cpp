#include <Arduino.h>
#include "pin_entry.h"
#include "buzzer.h"

namespace {
constexpr size_t kMaxPinLen = 16;
constexpr char kReprovisionCombo[] = "00009999";
char g_pin_buffer[kMaxPinLen + 1] = {0};
size_t g_pin_len = 0;
pin_auth_result_cb_t g_callback = nullptr;
reprovision_cb_t g_reprovision_cb = nullptr;

void clear_pin() {
  memset(g_pin_buffer, 0, sizeof(g_pin_buffer));
  g_pin_len = 0;
}
}

void pin_entry_init(pin_auth_result_cb_t callback) {
  g_callback = callback;
  clear_pin();
}

void pin_entry_set_reprovision_callback(reprovision_cb_t cb) {
  g_reprovision_cb = cb;
}

void pin_entry_handle_key(char key) {
  if (!key) {
    return;
  }

  if (key == '#') {
    g_pin_buffer[g_pin_len] = '\0';

    if (strcmp(g_pin_buffer, kReprovisionCombo) == 0) {
      Serial.println("[PIN_ENTRY] Reprovision combo detected");
      clear_pin();
      if (g_reprovision_cb) {
        g_reprovision_cb();
      }
      return;
    }

    Serial.print("[PIN_ENTRY] Submitting PIN: ");
    Serial.println(g_pin_buffer);
    pin_auth_verify(g_pin_buffer, g_callback);
    clear_pin();
    return;
  }

  if (key == '*') {
    clear_pin();
    Serial.println("PIN cleared");
    return;
  }

  if (key >= '0' && key <= '9') {
    if (g_pin_len < kMaxPinLen) {
      g_pin_buffer[g_pin_len] = key;
      ++g_pin_len;
      Serial.print("*");
      buzzer_keypress();
    }
    return;
  }
}
