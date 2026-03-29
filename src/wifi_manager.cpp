#include <Arduino.h>
#include <WiFi.h>

#include "wifi_manager.h"

namespace {
const IPAddress kDhcpAddress(0, 0, 0, 0);
const IPAddress kPrimaryDns(8, 8, 8, 8);
const IPAddress kSecondaryDns(1, 1, 1, 1);
}

bool wifi_manager_connect(const char* ssid, const char* password) {
  Serial.print("WiFi connecting to: ");
  Serial.println(ssid);

  if (WiFi.config(kDhcpAddress, kDhcpAddress, kDhcpAddress, kPrimaryDns, kSecondaryDns)) {
    Serial.print("WiFi DNS primary: ");
    Serial.println(kPrimaryDns);
    Serial.print("WiFi DNS secondary: ");
    Serial.println(kSecondaryDns);
  } else {
    Serial.println("WiFi DNS configuration failed, using network defaults");
  }

  WiFi.begin(ssid, password);

  for (int attempt = 1; attempt <= 5; ++attempt) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());

      Serial.print("WiFi gateway: ");
      Serial.println(WiFi.gatewayIP());
      Serial.print("WiFi subnet: ");
      Serial.println(WiFi.subnetMask());

      Serial.print("WiFi DNS current #1: ");
      Serial.println(WiFi.dnsIP(0));
      Serial.print("WiFi DNS current #2: ");
      Serial.println(WiFi.dnsIP(1));

      WiFiClient egress_test_client;
      bool egress_ok = egress_test_client.connect(IPAddress(1, 1, 1, 1), 443);
      Serial.print("WiFi egress test (1.1.1.1:443): ");
      Serial.println(egress_ok ? "ok" : "failed");
      if (egress_ok) {
        egress_test_client.stop();
      }
      return true;
    }
    Serial.print("WiFi retry ");
    Serial.println(attempt);
    delay(2000);
  }

  Serial.println("WiFi connection failed");
  return false;
}

bool wifi_manager_is_connected() {
  return WiFi.status() == WL_CONNECTED;
}

void wifi_manager_disconnect() {
  Serial.println("WiFi disconnect");
  WiFi.disconnect();
}
