#pragma once
#include <Arduino.h>

void ws_client_init(const char* base_url, const char* hardware_uuid);
void ws_client_start();
void ws_client_stop();
void ws_client_loop();
bool ws_client_is_connected();
void ws_client_set_reprovision_callback(void (*cb)());
void ws_client_set_unlock_callback(void (*cb)());
void ws_client_send_state(const char* status);
void ws_client_send_heartbeat();
