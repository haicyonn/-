#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <stdint.h>

bool App_Wifi_Connect(const char *ssid, const char *password);
bool App_Wifi_ScanOnce(void);
bool App_Wifi_IsConnected(void);
bool App_Wifi_PingHost(const char *host, uint32_t timeout_ms, uint32_t *elapsed_ms);

#endif
