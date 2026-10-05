#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_model.h"
#include "esp_err.h"

typedef enum {
    APP_NETWORK_IDLE,
    APP_NETWORK_CONNECTING,
    APP_NETWORK_CONNECTED,
    APP_NETWORK_DISCONNECTED,
    APP_NETWORK_FAILED,
} app_network_state_t;

typedef void (*app_network_callback_t)(app_network_state_t state, const char *detail, void *context);

esp_err_t app_network_init(app_network_callback_t callback, void *context);
esp_err_t app_network_connect(const char *ssid, const char *password);
esp_err_t app_network_disconnect(void);
bool app_network_is_connected(void);
esp_err_t app_network_scan(app_wifi_ap_t *results, size_t capacity, size_t *result_count);
