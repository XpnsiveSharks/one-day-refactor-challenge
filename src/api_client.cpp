#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "api_client.h"

namespace {
constexpr size_t kBaseUrlSize = 256;
constexpr size_t kUrlSize = 320;
constexpr size_t kBodySize = 256;
constexpr int kMaxRetries = 3;
constexpr int kRetryDelayMs = 2000;

char g_base_url[kBaseUrlSize] = {0};

bool is_https() {
  return strncmp(g_base_url, "https://", 8) == 0;
}
}  // namespace

void api_client_init(const char* base_url) {
  if (!base_url) {
    g_base_url[0] = '\0';
    return;
  }
  strncpy(g_base_url, base_url, kBaseUrlSize - 1);
  g_base_url[kBaseUrlSize - 1] = '\0';
}

bool api_client_register_device(const char* hardware_uuid, const char* token) {
  if (g_base_url[0] == '\0') {
    Serial.println("API client not initialized");
    return false;
  }
  if (!hardware_uuid || !token) {
    Serial.println("Missing hardware UUID or token");
    return false;
  }

  char url[kUrlSize];
  snprintf(url, sizeof(url), "%s/api/v1/devices/register", g_base_url);

  char body[kBodySize];
  snprintf(body, sizeof(body),
           "{\"hardware_uuid\":\"%s\",\"provisioning_token\":\"%s\"}",
           hardware_uuid, token);

  for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
    WiFiClientSecure secure_client;
    WiFiClient plain_client;
    HTTPClient http;

    if (is_https()) {
      secure_client.setInsecure();
      http.begin(secure_client, url);
    } else {
      http.begin(plain_client, url);
    }
    http.addHeader("Content-Type", "application/json");
    int status = http.POST(reinterpret_cast<uint8_t*>(body), strlen(body));
    Serial.print("Register attempt ");
    Serial.print(attempt);
    Serial.print(" status: ");
    Serial.println(status);
    http.end();

    if (status == 200) {
      return true;
    }
    if (status == 401 || status == 409) {
      return false;
    }
    if (attempt < kMaxRetries) {
      delay(kRetryDelayMs);
    }
  }

  return false;
}

bool api_client_send_tamper_alert(const char* hardware_uuid) {
  if (g_base_url[0] == '\0') {
    Serial.println("API client not initialized");
    return false;
  }
  if (!hardware_uuid) {
    Serial.println("Missing hardware UUID");
    return false;
  }

  char url[kUrlSize];
  snprintf(url, sizeof(url), "%s/api/v1/devices/tamper", g_base_url);

  char body[kBodySize];
  snprintf(body, sizeof(body), "{\"hardware_uuid\":\"%s\"}", hardware_uuid);

  WiFiClientSecure secure_client;
  WiFiClient plain_client;
  HTTPClient http;

  if (is_https()) {
    secure_client.setInsecure();
    http.begin(secure_client, url);
  } else {
    http.begin(plain_client, url);
  }
  http.addHeader("Content-Type", "application/json");
  int status = http.POST(reinterpret_cast<uint8_t*>(body), strlen(body));
  Serial.print("Tamper alert status: ");
  Serial.println(status);
  http.end();

  return status == 200;
}
