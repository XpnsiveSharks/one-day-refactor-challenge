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
constexpr uint16_t kHttpsPort = 443;
constexpr uint32_t kSocketTimeoutSeconds = 15;
constexpr const char* kRailwayHost = "web-production-1770.up.railway.app";
const IPAddress kRailwayFallbackIp(66, 33, 22, 1);

char g_base_url[kBaseUrlSize] = {0};

bool is_https() {
  return strncmp(g_base_url, "https://", 8) == 0;
}

bool should_use_railway_ip_fallback() {
  return strstr(g_base_url, kRailwayHost) != nullptr;
}

bool is_six_digit_token(const char* token) {
  if (!token) {
    return false;
  }

  for (size_t i = 0; i < 6; ++i) {
    if (token[i] < '0' || token[i] > '9') {
      return false;
    }
  }
  return token[6] == '\0';
}

int post_json_via_railway_ip_fallback(
    const char* endpoint_path,
    const char* body,
    String* response_out
) {
  WiFiClientSecure secure_client;
  secure_client.setInsecure();
  secure_client.setHandshakeTimeout(kSocketTimeoutSeconds);
  secure_client.setTimeout(kSocketTimeoutSeconds);

  if (!secure_client.connect(kRailwayFallbackIp, kHttpsPort, kRailwayHost, nullptr, nullptr, nullptr)) {
    Serial.println("IP fallback TLS connect failed");
    return -1;
  }

  String request;
  request.reserve(strlen(body) + 256);
  request += "POST ";
  request += endpoint_path;
  request += " HTTP/1.1\r\n";
  request += "Host: ";
  request += kRailwayHost;
  request += "\r\n";
  request += "User-Agent: smartvault-esp32/1.0\r\n";
  request += "Connection: close\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Content-Length: ";
  request += String(strlen(body));
  request += "\r\n\r\n";
  request += body;

  if (secure_client.print(request) == 0) {
    Serial.println("IP fallback request send failed");
    secure_client.stop();
    return -1;
  }

  String status_line = secure_client.readStringUntil('\n');
  status_line.trim();
  int first_space = status_line.indexOf(' ');
  if (first_space < 0 || status_line.length() < static_cast<unsigned int>(first_space + 4)) {
    Serial.println("IP fallback invalid HTTP response");
    secure_client.stop();
    return -1;
  }

  int status = status_line.substring(first_space + 1, first_space + 4).toInt();

  while (secure_client.connected()) {
    String header_line = secure_client.readStringUntil('\n');
    if (header_line == "\r" || header_line.length() == 0) {
      break;
    }
  }

  if (response_out) {
    *response_out = secure_client.readString();
  }
  secure_client.stop();
  return status;
}

int api_client_post_json(const char* endpoint_path, const char* body, String* response_out) {
  char url[kUrlSize];
  snprintf(url, sizeof(url), "%s%s", g_base_url, endpoint_path);

  WiFiClientSecure secure_client;
  WiFiClient plain_client;
  HTTPClient http;

  if (is_https()) {
    secure_client.setInsecure();
    secure_client.setHandshakeTimeout(kSocketTimeoutSeconds);
    secure_client.setTimeout(kSocketTimeoutSeconds);
    http.begin(secure_client, url);
  } else {
    http.begin(plain_client, url);
  }

  http.addHeader("Content-Type", "application/json");
  int status = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body)), strlen(body));
  if (status > 0 && response_out) {
    *response_out = http.getString();
  }
  http.end();

  if (status > 0 || !is_https() || !should_use_railway_ip_fallback()) {
    return status;
  }

  Serial.println("Primary HTTPS request failed, trying IP fallback");
  int fallback_status = post_json_via_railway_ip_fallback(endpoint_path, body, response_out);
  if (fallback_status > 0) {
    Serial.print("IP fallback status: ");
    Serial.println(fallback_status);
  }
  return fallback_status;
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

bool api_client_register_device(
    const char* hardware_uuid,
    const char* token,
    char* vault_id_out,
    size_t vault_id_len
) {
  if (g_base_url[0] == '\0') {
    Serial.println("API client not initialized");
    return false;
  }
  if (!hardware_uuid || !token) {
    Serial.println("Missing hardware UUID or token");
    return false;
  }
  if (!is_six_digit_token(token)) {
    Serial.println("Invalid provisioning token format");
    return false;
  }

  char body[kBodySize];
  snprintf(body, sizeof(body),
           "{\"hardware_uuid\":\"%s\",\"provisioning_token\":\"%s\"}",
           hardware_uuid, token);

  for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
    String response;
    int status = api_client_post_json("/api/v1/devices/register", body, &response);
    Serial.print("Register attempt ");
    Serial.print(attempt);
    Serial.print(" status: ");
    Serial.println(status);

    if (status == 200) {
      Serial.print("Registration response: ");
      Serial.println(response);

      int vault_id_start = response.indexOf("\"vault_id\":\"") + 12;
      int vault_id_end = response.indexOf("\"", vault_id_start);
      if (vault_id_start > 12 && vault_id_end > vault_id_start && vault_id_out && vault_id_len > 0) {
        String vault_id = response.substring(vault_id_start, vault_id_end);
        strncpy(vault_id_out, vault_id.c_str(), vault_id_len - 1);
        vault_id_out[vault_id_len - 1] = '\0';
        Serial.print("Extracted vault_id: ");
        Serial.println(vault_id_out);
      }
      return true;
    }

    if (status == 409) {
      Serial.println("Device already registered (409)");
      Serial.println("Attempting backend deprovision");

      bool deprovisioned = api_client_deprovision_device(hardware_uuid);
      if (deprovisioned) {
        Serial.println("Deprovisioned old vault, retrying register");
        delay(1000);
        continue;
      }
      Serial.println("Deprovision failed, cannot register");
      return false;
    }

    if (status == 401) {
      Serial.println("Invalid provisioning token (401)");
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

  char body[kBodySize];
  snprintf(body, sizeof(body), "{\"hardware_uuid\":\"%s\"}", hardware_uuid);

  int status = api_client_post_json("/api/v1/devices/tamper", body, nullptr);
  Serial.print("Tamper alert status: ");
  Serial.println(status);

  return status == 200;
}

bool api_client_deprovision_device(const char* hardware_uuid) {
  if (g_base_url[0] == '\0') {
    Serial.println("API client not initialized");
    return false;
  }
  if (!hardware_uuid) {
    Serial.println("Missing hardware UUID");
    return false;
  }

  char body[kBodySize];
  snprintf(body, sizeof(body), "{\"hardware_uuid\":\"%s\"}", hardware_uuid);

  Serial.println("Attempting backend deprovision");

  for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
    int status = api_client_post_json("/api/v1/devices/deprovision", body, nullptr);
    Serial.print("Deprovision attempt ");
    Serial.print(attempt);
    Serial.print(" status: ");
    Serial.println(status);

    if (status == 200) {
      Serial.println("Deprovisioned device");
      return true;
    }
    if (status == 404) {
      Serial.println("Device not registered");
      return true;
    }
    if (attempt < kMaxRetries) {
      delay(5000);
    }
  }

  Serial.println("Deprovision failed after retries");
  return false;
}
