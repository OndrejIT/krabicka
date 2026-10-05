#pragma once

#include <stddef.h>

#include "app_model.h"
#include "esp_err.h"

esp_err_t app_data_fetch_weather(double latitude, double longitude, app_weather_t *weather);
esp_err_t app_data_fetch_btc(const char *currency, app_btc_t *btc);
esp_err_t app_data_fetch_claude_usage(const char *setup_token, app_claude_usage_t *usage);
esp_err_t app_data_geocode(const char *query, app_geo_result_t *results, size_t capacity, size_t *result_count);
const char *app_data_last_error(void);
const char *app_weather_description(int weather_code);
