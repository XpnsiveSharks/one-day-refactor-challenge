#pragma once
#include <Arduino.h>

void api_client_init(const char* base_url);
bool api_client_register_device(const char* hardware_uuid, const char* token);
bool api_client_send_tamper_alert(const char* hardware_uuid);
