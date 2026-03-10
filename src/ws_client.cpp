#include <Arduino.h>
#include <WebSocketsClient.h>
#include "ws_client.h"

namespace {
WebSocketsClient webSocket;
String g_ws_url;
String g_host;
String g_path;
uint16_t g_port = 0;
bool g_use_ssl = false;
bool g_started = false;
bool g_connected = false;
unsigned long g_last_reconnect_attempt = 0;
void (*g_reprovision_callback)() = nullptr;
void (*g_unlock_callback)() = nullptr;

String build_ws_url(const char* base_url, const char* hardware_uuid) {
  String ws_url = base_url;
  if (ws_url.startsWith("https://")) {
    ws_url.replace("https://", "wss://");
  } else if (ws_url.startsWith("http://")) {
    ws_url.replace("http://", "ws://");
  }

  if (ws_url.endsWith("/")) {
    ws_url.remove(ws_url.length() - 1);
  }

  ws_url += "/api/v1/ws/vault?hardware_uuid=";
  ws_url += hardware_uuid;
  return ws_url;
}

bool parse_ws_url(const String& url, bool* use_ssl, String* host, uint16_t* port, String* path) {
  size_t scheme_len = 0;
  if (url.startsWith("wss://")) {
    *use_ssl = true;
    scheme_len = 6;
  } else if (url.startsWith("ws://")) {
    *use_ssl = false;
    scheme_len = 5;
  } else {
    return false;
  }

  String rest = url.substring(scheme_len);
  int slash_index = rest.indexOf('/');
  String host_port = (slash_index >= 0) ? rest.substring(0, slash_index) : rest;
  String path_value = (slash_index >= 0) ? rest.substring(slash_index) : "/";

  int colon_index = host_port.indexOf(':');
  String host_value = host_port;
  uint16_t port_value = *use_ssl ? 443 : 80;

  if (colon_index >= 0) {
    host_value = host_port.substring(0, colon_index);
    String port_str = host_port.substring(colon_index + 1);
    if (port_str.length() > 0) {
      port_value = static_cast<uint16_t>(port_str.toInt());
    }
  }

  if (host_value.length() == 0) {
    return false;
  }

  *host = host_value;
  *port = port_value;
  *path = path_value;
  return true;
}

void handle_text_message(const String& message) {
  if (message.indexOf("remote_unlock") >= 0 || message.indexOf("\"UNLOCK\"") >= 0) {
    Serial.println("CMD: remote_unlock");
    if (g_unlock_callback) {
      g_unlock_callback();
    }
  }

  if (message.indexOf("buzzer_off") >= 0) {
    Serial.println("CMD: buzzer_off");
  }

  if (message.indexOf("reprovision") >= 0) {
    Serial.println("CMD: reprovision");
    if (g_reprovision_callback) {
      g_reprovision_callback();
    }
  }
}

void handle_ws_event(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      g_connected = true;
      Serial.println("WS connected");
      ws_client_send_heartbeat();
      break;
    case WStype_DISCONNECTED:
      g_connected = false;
      Serial.println("WS disconnected");
      break;
    case WStype_TEXT: {
      String message;
      message.reserve(length);
      for (size_t i = 0; i < length; ++i) {
        message += static_cast<char>(payload[i]);
      }
      handle_text_message(message);
      break;
    }
    default:
      break;
  }
}

void start_connection() {
  if (!parse_ws_url(g_ws_url, &g_use_ssl, &g_host, &g_port, &g_path)) {
    Serial.println("WS URL parse failed");
    return;
  }

  Serial.print("WS connecting: ");
  Serial.println(g_ws_url);

  if (g_use_ssl) {
    webSocket.beginSSL(g_host.c_str(), g_port, g_path.c_str(), "");
  } else {
    webSocket.begin(g_host.c_str(), g_port, g_path.c_str());
  }

  webSocket.onEvent(handle_ws_event);
}
}  // namespace

void ws_client_init(const char* base_url, const char* hardware_uuid) {
  g_ws_url = build_ws_url(base_url, hardware_uuid);
}

void ws_client_start() {
  g_started = true;
  g_last_reconnect_attempt = 0;
  start_connection();
}

void ws_client_stop() {
  g_started = false;
  g_connected = false;
  webSocket.disconnect();
  Serial.println("WS stopped");
}

void ws_client_loop() {
  if (!g_started) {
    return;
  }

  webSocket.loop();

  unsigned long now = millis();
  static unsigned long last_heartbeat = 0;
  if (g_connected && now - last_heartbeat >= 30000) {
    last_heartbeat = now;
    ws_client_send_heartbeat();
  }

  if (!g_connected) {
    if (now - g_last_reconnect_attempt >= 5000) {
      g_last_reconnect_attempt = now;
      start_connection();
    }
  }
}

bool ws_client_is_connected() {
  return g_connected;
}

void ws_client_set_reprovision_callback(void (*cb)()) {
  g_reprovision_callback = cb;
}

void ws_client_set_unlock_callback(void (*cb)()) {
  g_unlock_callback = cb;
}

void ws_client_send_state(const char* status) {
  if (!g_connected) {
    return;
  }

  String message = "{\"type\":\"state_update\",\"status\":\"";
  message += status;
  message += "\"}";
  webSocket.sendTXT(message);
}

void ws_client_send_heartbeat() {
  if (!g_connected) {
    return;
  }

  webSocket.sendTXT("{\"type\":\"heartbeat\"}");
}
