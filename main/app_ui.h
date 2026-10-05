#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_model.h"
#include "app_network.h"

typedef struct {
    void (*request_wifi_scan)(void *context);
    void (*connect_wifi)(const char *ssid, const char *password, void *context);
    void (*forget_wifi)(void *context);
    void (*request_geocode)(const char *query, void *context);
    void (*select_location)(const app_geo_result_t *location, void *context);
    void (*select_timezone)(const char *name, const char *posix_tz, void *context);
    void (*select_currency)(const char *currency, void *context);
    void (*set_brightness)(uint8_t brightness, void *context);
    void (*request_refresh)(void *context);
    void (*request_claude_refresh)(void *context);
    void (*enable_claude_setup)(void *context);
    void *context;
} app_ui_callbacks_t;

void app_ui_init(const app_config_t *config, const app_ui_callbacks_t *callbacks);
void app_ui_show_welcome(void);
void app_ui_show_dashboard(const app_config_t *config);
void app_ui_show_wifi_scanning(void);
void app_ui_show_wifi_results(const app_wifi_ap_t *results, size_t count, const char *error);
void app_ui_set_network_state(app_network_state_t state, const char *detail);
void app_ui_set_internet_available(bool available);
void app_ui_set_data_refreshing(bool refreshing);
void app_ui_set_btc_status(const char *message, bool error);
void app_ui_update_weather(const app_weather_t *weather);
void app_ui_clear_weather_cache(void);
void app_ui_update_btc(const app_btc_t *btc);
void app_ui_update_claude(const app_claude_usage_t *usage);
void app_ui_set_claude_status(const char *message, bool error);
void app_ui_set_claude_configured(bool configured);
void app_ui_set_device_address(const char *address);
void app_ui_show_geocode_results(const app_geo_result_t *results, size_t count, const char *error);
