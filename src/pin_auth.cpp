#include <Arduino.h>
#include <HTTPClient.h>
#include "pin_auth.h"

namespace {
constexpr size_t kUrlMaxLen = 256;
constexpr size_t kUuidMaxLen = 128;
char g_base_url[kUrlMaxLen] = {0};
char g_hardware_uuid[kUuidMaxLen] = {0};
}

void pin_auth_init(const char* base_url, const char* hardware_uuid) {
  if (base_url) {
    strncpy(g_base_url, base_url, sizeof(g_base_url) - 1);
    g_base_url[sizeof(g_base_url) - 1] = '\0';
  }
  if (hardware_uuid) {
    strncpy(g_hardware_uuid, hardware_uuid, sizeof(g_hardware_uuid) - 1);
    g_hardware_uuid[sizeof(g_hardware_uuid) - 1] = '\0';
  }
}

void pin_auth_verify(const char* pin, pin_auth_result_cb_t callback) {
  if (!callback) {
    return;
  }
  if (!pin) {
    callback(false);
    return;
  }

  const String url = String(g_base_url) + "/api/v1/devices/verify-pin";
  const String payload = String("{\"hardware_uuid\":\"") +
                         g_hardware_uuid + "\",\"pin\":\"" + pin + "\"}";

  for (int attempt = 0; attempt < 3; ++attempt) {
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int status = http.POST(payload);

    if (status > 0) {
      Serial.print("PIN verify HTTP status: ");
      Serial.println(status);
    } else {
      Serial.print("PIN verify HTTP failed: ");
      Serial.println(status);
    }

    if (status == 200) {
      http.end();
      callback(true);
      return;
    }
    if (status == 401 || status == 403 || status == 409) {
      http.end();
      callback(false);
      return;
    }

    http.end();
  }

  callback(false);
}
