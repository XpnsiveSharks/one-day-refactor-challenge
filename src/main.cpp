#include <Arduino.h>
#include <WiFi.h>
#include "api_client.h"
#include "buzzer.h"
#include "keypad.h"
#include "nvs_storage.h"
#include "pin_auth.h"
#include "pin_entry.h"
#include "provisioning.h"
#include "solenoid.h"
#include "wifi_manager.h"
#include "ws_client.h"

namespace {
constexpr size_t kSsidMaxLen = 33;
constexpr size_t kPasswordMaxLen = 65;
constexpr size_t kTokenMaxLen = 129;
constexpr size_t kApiUrlMaxLen = 129;

String g_api_url;
String g_mac;
static bool g_authorized_unlock = false;
static bool g_tamper_alarm_active = false;

void handle_reprovision() {
  Serial.println("Reprovisioning...");
  ws_client_stop();
  nvs_storage_erase_all();
  ESP.restart();
}

void handle_remote_unlock() {
  g_authorized_unlock = true;
  solenoid_unlock();
}

void handle_pin_result(bool accepted) {
  if (accepted) {
    Serial.println("Access granted");
    buzzer_success();
    g_authorized_unlock = true;
    solenoid_unlock();
  } else {
    Serial.println("Access denied");
    buzzer_fail();
  }
}

void start_normal_mode(const char* api_url, const char* mac) {
  g_api_url = api_url;
  g_mac = mac;
  pin_auth_init(g_api_url.c_str(), g_mac.c_str());
  pin_entry_init(handle_pin_result);
  pin_entry_set_reprovision_callback(handle_reprovision);
  ws_client_init(g_api_url.c_str(), g_mac.c_str());
  ws_client_set_reprovision_callback(handle_reprovision);
  ws_client_set_unlock_callback(handle_remote_unlock);
  ws_client_start();
}

void handle_provisioning_done(const ProvisioningData &data) {
  Serial.println("Provisioning complete");
  Serial.print("SSID: ");
  Serial.println(data.ssid);
  Serial.print("Token: ");
  Serial.println(data.token);
  Serial.print("API URL: ");
  Serial.println(data.api_url);

  bool wifi_connected = wifi_manager_connect(data.ssid, data.password);
  if (!wifi_connected) {
    Serial.println("WiFi failed");
    return;
  }

  Serial.println("WiFi connected");
  api_client_init(data.api_url);
  String mac = WiFi.macAddress();
  bool registered = api_client_register_device(mac.c_str(), data.token);
  if (!registered) {
    Serial.println("Device registration failed");
    return;
  }

  Serial.println("Device registered");
  nvs_storage_save_wifi(data.ssid, data.password);
  nvs_storage_save_token(data.token);
  nvs_storage_save_api_url(data.api_url);
  nvs_storage_set_provisioned(true);
  start_normal_mode(data.api_url, mac.c_str());
}
}  // namespace

void setup() {
  Serial.begin(115200);
  buzzer_init();
  keypad_init();
  solenoid_init();
  nvs_storage_init();
  pin_entry_init(handle_pin_result);
  pin_entry_set_reprovision_callback(handle_reprovision);

  if (nvs_storage_is_provisioned()) {
    char ssid[kSsidMaxLen] = {0};
    char password[kPasswordMaxLen] = {0};
    char token[kTokenMaxLen] = {0};
    char api_url[kApiUrlMaxLen] = {0};

    bool loaded = nvs_storage_load_wifi(ssid, sizeof(ssid), password, sizeof(password))
               && nvs_storage_load_token(token, sizeof(token))
               && nvs_storage_load_api_url(api_url, sizeof(api_url));

    if (loaded) {
      bool connected = wifi_manager_connect(ssid, password);
      if (connected) {
        Serial.println("WiFi connected");
        api_client_init(api_url);
        String mac = WiFi.macAddress();
        start_normal_mode(api_url, mac.c_str());
        Serial.println("Loaded from NVS");
        return;
      }
      Serial.println("WiFi failed");
    } else {
      Serial.println("NVS data missing");
    }
  }

  provisioning_start(handle_provisioning_done);
  Serial.println("Provisioning started");
}

void loop() {
  if (provisioning_is_active()) {
    provisioning_loop();
    delay(10);
    return;
  }

  ws_client_loop();
  solenoid_loop();

  bool sensor_locked = solenoid_sensor_locked();

  static bool last_sensor_locked = sensor_locked;
  if (sensor_locked != last_sensor_locked) {
    last_sensor_locked = sensor_locked;
    if (!sensor_locked) {
      if (!g_authorized_unlock) {
        Serial.println("TAMPER DETECTED");
        g_tamper_alarm_active = true;
        buzzer_tamper();
        api_client_send_tamper_alert(g_mac.c_str());
      }
    } else {
      g_authorized_unlock = false;
      g_tamper_alarm_active = false;
    }
    Serial.println(sensor_locked ? "Lock: LOCKED" : "Lock: OPEN");
    ws_client_send_state(sensor_locked ? "LOCKED" : "UNLOCKED");
  }

  char key = keypad_get_key();
  if (key) {
    pin_entry_handle_key(key);
  }
  digitalWrite(BUZZER_PIN, g_tamper_alarm_active ? HIGH : LOW);
  delay(10);
}
