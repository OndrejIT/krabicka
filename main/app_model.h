#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define APP_WIFI_SSID_MAX 32
#define APP_WIFI_PASSWORD_MAX 64
#define APP_CITY_MAX 63
#define APP_TIMEZONE_MAX 47
#define APP_POSIX_TZ_MAX 63
#define APP_CURRENCY_MAX 3
#define APP_CLAUDE_TOKEN_MAX 383
#define APP_WEATHER_DAYS 5
#define APP_BTC_POINTS_MAX 96
#define APP_WIFI_AP_MAX 12
#define APP_GEO_RESULTS_MAX 5

typedef struct {
    bool has_wifi;
    char wifi_ssid[APP_WIFI_SSID_MAX + 1];
    char wifi_password[APP_WIFI_PASSWORD_MAX + 1];
    char city[APP_CITY_MAX + 1];
    double latitude;
    double longitude;
    char timezone[APP_TIMEZONE_MAX + 1];
    char posix_tz[APP_POSIX_TZ_MAX + 1];
    char btc_currency[APP_CURRENCY_MAX + 1];
    bool has_claude_token;
    char claude_token[APP_CLAUDE_TOKEN_MAX + 1];
    uint8_t brightness;
} app_config_t;

typedef struct {
    char ssid[APP_WIFI_SSID_MAX + 1];
    int8_t rssi;
    bool secured;
} app_wifi_ap_t;

typedef struct {
    char name[APP_CITY_MAX + 1];
    char country[40];
    char timezone[APP_TIMEZONE_MAX + 1];
    double latitude;
    double longitude;
} app_geo_result_t;

typedef struct {
    bool valid;
    double temperature;
    double apparent_temperature;
    double temperature_min;
    double temperature_max;
    int precipitation_probability;
    int weather_code;
    int daily_code[APP_WEATHER_DAYS];
    double daily_min[APP_WEATHER_DAYS];
    double daily_max[APP_WEATHER_DAYS];
    int daily_precipitation[APP_WEATHER_DAYS];
    size_t daily_count;
    time_t updated_at;
} app_weather_t;

typedef struct {
    float open;
    float high;
    float low;
    float close;
} app_btc_candle_t;

typedef struct {
    bool valid;
    double price;
    double change_percent_24h;
    char currency[APP_CURRENCY_MAX + 1];
    app_btc_candle_t candles[APP_BTC_POINTS_MAX];
    size_t point_count;
    time_t updated_at;
} app_btc_t;

typedef struct {
    bool valid;
    float five_hour_used_percent;
    float seven_day_used_percent;
    time_t five_hour_reset;
    time_t seven_day_reset;
    time_t updated_at;
} app_claude_usage_t;
