#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#include "provisioning.h"

const char* kApiUrl = "https://web-production-0146f.up.railway.app";

namespace {
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApNetmask(255, 255, 255, 0);

DNSServer dns_server;
WebServer web_server(80);
provisioning_done_cb_t done_callback = nullptr;
bool provisioning_active = false;
bool stop_pending = false;
bool callback_pending = false;
ProvisioningData callback_data = {};

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
    ".input-wrapper{position:relative}"
    "input{width:100%;background:#0a0a0a;border:1.5px solid #2a2a2a;"
    "border-radius:14px;color:#f5f5f5;font-size:14px;font-weight:500;"
    "padding:14px 16px;outline:none;transition:border-color 0.15s}"
    ".input-wrapper input{padding-right:44px}"
    "input:focus{border-color:#BFFF00}"
    "input::placeholder{color:#3a3a3a}"
    ".toggle-btn{position:absolute;right:12px;top:50%;transform:translateY(-50%);"
    "background:none;border:none;cursor:pointer;padding:6px;display:flex}"
    ".toggle-btn svg{width:20px;height:20px;stroke:#a3a3a3;transition:stroke 0.15s}"
    ".toggle-btn:active svg{stroke:#BFFF00}"
    "button[type=submit]{margin-top:24px;width:100%;background:#BFFF00;border:none;"
    "border-radius:14px;color:#000;font-size:14px;font-weight:900;"
    "letter-spacing:1.5px;text-transform:uppercase;padding:16px;"
    "cursor:pointer;transition:opacity 0.15s}"
    "button[type=submit]:active{opacity:0.85}"
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
    "<div class='input-wrapper'>"
    "<input id='password' name='password' type='password' placeholder='Password' required>"
    "<button type='button' class='toggle-btn' onclick='togglePw()' aria-label='Show password'>"
    "<svg viewBox='0 0 24 24' fill='none' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path id='eye-path' d='M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z'/>"
    "<circle id='eye-circle' cx='12' cy='12' r='3'/>"
    "<line id='eye-slash' d='M1 1l22 22' style='display:none'/>"
    "</svg>"
    "</button>"
    "</div>"
    "<label for='token'>Provisioning Token</label>"
    "<input id='token' name='token' type='text' placeholder='Paste token from app' required>"
    "<button type='submit'>Connect Device</button>"
    "</form>"
    "</div>"
    "<script>"
    "function togglePw(){"
    "const p=document.getElementById('password');"
    "const path=document.getElementById('eye-path');"
    "const circle=document.getElementById('eye-circle');"
    "const slash=document.getElementById('eye-slash');"
    "if(p.type==='password'){"
    "p.type='text';"
    "path.style.display='none';"
    "circle.style.display='none';"
    "slash.style.display='block';"
    "}else{"
    "p.type='password';"
    "path.style.display='block';"
    "circle.style.display='block';"
    "slash.style.display='none';"
    "}}"
    "</script>"
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

  callback_data = data;
  callback_pending = true;
  stop_pending = true;
}

void handle_not_found() {
  web_server.sendHeader("Location", "/", true);
  web_server.send(302, "text/plain", "");
}

void handle_android_captive() {
  web_server.send(204, "text/plain", "");
}

void handle_ios_captive() {
  web_server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}

void handle_windows_captive() {
  web_server.send(200, "text/plain", "Microsoft Connect Test");
}
}  // namespace

const char* provisioning_get_api_url() {
  return kApiUrl;
}

void provisioning_start(provisioning_done_cb_t callback) {
  if (provisioning_active) {
    return;
  }

  done_callback = callback;
  stop_pending = false;
  callback_pending = false;

  WiFi.mode(WIFI_AP);
  String ssid = build_ap_ssid();
  WiFi.softAPConfig(kApIp, kApIp, kApNetmask);
  WiFi.softAP(ssid.c_str());

  dns_server.start(53, "*", kApIp);

  web_server.on("/", HTTP_GET, handle_root);
  web_server.on("/provision", HTTP_POST, handle_provision);
  web_server.onNotFound(handle_not_found);
  web_server.on("/generate_204", HTTP_GET, handle_android_captive);
  web_server.on("/gen_204", HTTP_GET, handle_android_captive);
  web_server.on("/hotspot-detect.html", HTTP_GET, handle_ios_captive);
  web_server.on("/library/test/success.html", HTTP_GET, handle_ios_captive);
  web_server.on("/connecttest.txt", HTTP_GET, handle_windows_captive);
  web_server.on("/ncsi.txt", HTTP_GET, handle_windows_captive);
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
    if (callback_pending && done_callback) {
      callback_pending = false;
      done_callback(callback_data);
    }
  }
}

bool provisioning_is_active() {
  return provisioning_active;
}
