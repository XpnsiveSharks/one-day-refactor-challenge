#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#include "provisioning.h"

namespace {
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApNetmask(255, 255, 255, 0);

DNSServer dns_server;
WebServer web_server(80);
provisioning_done_cb_t done_callback = nullptr;
bool provisioning_active = false;
bool stop_pending = false;

const char kProvisioningPage[] PROGMEM =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SmartVault Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "background:#0a0a0a;color:#f5f5f5;min-height:100vh;"
    "display:flex;align-items:center;justify-content:center;padding:24px}"
    ".card{width:100%;max-width:400px;background:#141414;"
    "border:1px solid #2a2a2a;border-radius:16px;padding:32px}"
    ".brand{margin-bottom:28px}"
    ".brand-title{font-size:22px;font-weight:900;letter-spacing:-0.5px;color:#f5f5f5}"
    ".brand-sub{font-size:10px;font-weight:700;letter-spacing:4px;"
    "text-transform:uppercase;color:#a3a3a3;margin-top:4px}"
    ".accent{color:#BFFF00}"
    "label{display:block;font-size:11px;font-weight:700;letter-spacing:1.5px;"
    "text-transform:uppercase;color:#a3a3a3;margin:16px 0 6px}"
    "input{width:100%;background:#0a0a0a;border:1.5px solid #2a2a2a;"
    "border-radius:14px;color:#f5f5f5;font-size:14px;font-weight:500;"
    "padding:14px 16px;outline:none;transition:border-color 0.15s}"
    "input:focus{border-color:#BFFF00}"
    "input::placeholder{color:#3a3a3a}"
    "button{margin-top:24px;width:100%;background:#BFFF00;border:none;"
    "border-radius:14px;color:#000;font-size:14px;font-weight:900;"
    "letter-spacing:1.5px;text-transform:uppercase;padding:16px;"
    "cursor:pointer;transition:opacity 0.15s}"
    "button:active{opacity:0.85}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='card'>"
    "<div class='brand'>"
    "<div class='brand-title'>Smart<span class='accent'>Vault</span></div>"
    "<div class='brand-sub'>Device Setup</div>"
    "</div>"
    "<form method='POST' action='/provision'>"
    "<label for='ssid'>WiFi Network</label>"
    "<input id='ssid' name='ssid' type='text' placeholder='Network name' required>"
    "<label for='password'>WiFi Password</label>"
    "<input id='password' name='password' type='password' placeholder='Password' required>"
    "<label for='token'>Provisioning Token</label>"
    "<input id='token' name='token' type='text' placeholder='Paste token from app' required>"
    "<label for='api_url'>API URL</label>"
    "<input id='api_url' name='api_url' type='text' placeholder='https://...' required>"
    "<button type='submit'>Connect Device</button>"
    "</form>"
    "</div>"
    "</body>"
    "</html>";

String build_ap_ssid() {
  String mac = WiFi.macAddress();
  String suffix = mac.length() >= 17 ? mac.substring(9) : mac;
  suffix.replace(":", "");
  suffix.toUpperCase();
  return "SmartVault-" + suffix;
}

void copy_field(const String &value, char *dest, size_t size) {
  if (size == 0) {
    return;
  }
  size_t copy_len = value.length();
  if (copy_len >= size) {
    copy_len = size - 1;
  }
  memcpy(dest, value.c_str(), copy_len);
  dest[copy_len] = '\0';
}

void handle_root() {
  web_server.send(200, "text/html", kProvisioningPage);
}

void handle_provision() {
  ProvisioningData data = {};
  copy_field(web_server.arg("ssid"), data.ssid, sizeof(data.ssid));
  copy_field(web_server.arg("password"), data.password, sizeof(data.password));
  copy_field(web_server.arg("token"), data.token, sizeof(data.token));
  copy_field(web_server.arg("api_url"), data.api_url, sizeof(data.api_url));

  web_server.send(200, "text/html",
                  "<!DOCTYPE html>"
                  "<html lang='en'>"
                  "<head>"
                  "<meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>SmartVault Setup</title>"
                  "<style>"
                  "*{box-sizing:border-box;margin:0;padding:0}"
                  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
                  "background:#0a0a0a;color:#f5f5f5;min-height:100vh;"
                  "display:flex;align-items:center;justify-content:center;padding:24px}"
                  ".card{width:100%;max-width:400px;background:#141414;"
                  "border:1px solid #2a2a2a;border-radius:16px;padding:32px;text-align:center}"
                  ".icon{font-size:40px;margin-bottom:16px;color:#BFFF00}"
                  ".title{font-size:20px;font-weight:900;color:#BFFF00;letter-spacing:-0.5px}"
                  ".sub{font-size:13px;color:#a3a3a3;margin-top:10px;line-height:1.6}"
                  "</style>"
                  "</head>"
                  "<body>"
                  "<div class='card'>"
                  "<div class='icon'>&#10003;</div>"
                  "<div class='title'>Setup Complete</div>"
                  "<div class='sub'>Device is connecting to your network."
                  "<br>You can close this page and return to the app.</div>"
                  "</div>"
                  "</body>"
                  "</html>");

  if (done_callback) {
    done_callback(data);
  }

  stop_pending = true;
}

void handle_not_found() {
  web_server.sendHeader("Location", "/", true);
  web_server.send(302, "text/plain", "");
}
}  // namespace

void provisioning_start(provisioning_done_cb_t callback) {
  if (provisioning_active) {
    return;
  }

  done_callback = callback;
  stop_pending = false;

  WiFi.mode(WIFI_AP);
  String ssid = build_ap_ssid();
  WiFi.softAPConfig(kApIp, kApIp, kApNetmask);
  WiFi.softAP(ssid.c_str());

  dns_server.start(53, "*", kApIp);

  web_server.on("/", HTTP_GET, handle_root);
  web_server.on("/provision", HTTP_POST, handle_provision);
  web_server.onNotFound(handle_not_found);
  web_server.begin();

  provisioning_active = true;
}

void provisioning_stop() {
  if (!provisioning_active) {
    return;
  }

  dns_server.stop();
  web_server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  provisioning_active = false;
  stop_pending = false;
}

void provisioning_loop() {
  if (!provisioning_active) {
    return;
  }

  dns_server.processNextRequest();
  web_server.handleClient();

  if (stop_pending) {
    provisioning_stop();
  }
}

bool provisioning_is_active() {
  return provisioning_active;
}
