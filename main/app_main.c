#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_data.h"
#include "app_network.h"
#include "app_storage.h"
#include "app_ui.h"
#include "app_web.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"

static const char *TAG = "dashboard";

static app_config_t s_config;
static app_weather_t s_weather;
static app_btc_t s_btc;
static app_claude_usage_t s_claude;
static TaskHandle_t s_refresh_task;
static SemaphoreHandle_t s_config_mutex;
static bool s_time_started;
static bool s_credentials_pending;

#define REFRESH_ALL BIT0
#define REFRESH_CLAUDE BIT1
#define DATA_REFRESH_INTERVAL_MS (15 * 60 * 1000)

typedef struct {
    char ssid[APP_WIFI_SSID_MAX + 1];
    char password[APP_WIFI_PASSWORD_MAX + 1];
} connect_request_t;

typedef struct {
    char query[APP_CITY_MAX + 1];
} geocode_request_t;

static void request_refresh(void);
static void request_claude_refresh(void);

static void start_time_sync(void)
{
    setenv("TZ", s_config.posix_tz, 1);
    tzset();
    if (s_time_started) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "time.cloudflare.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    s_time_started = true;
}

static void refresh_task(void *argument)
{
    (void)argument;
    TickType_t last_weather = 0;
    TickType_t last_btc = 0;
    TickType_t last_claude = 0;
    for (;;) {
        uint32_t notifications = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notifications, pdMS_TO_TICKS(30000));
        if (!app_network_is_connected()) continue;

        time_t wall_clock = time(NULL);
        if (wall_clock < 1704067200) {
            ESP_LOGI(TAG, "Waiting for SNTP before HTTPS requests");
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        bool forced_all = (notifications & REFRESH_ALL) != 0 || last_btc == 0;
        bool forced_claude = forced_all || (notifications & REFRESH_CLAUDE) != 0;
        bool refresh_btc = forced_all ||
                           now - last_btc >= pdMS_TO_TICKS(DATA_REFRESH_INTERVAL_MS);
        bool refresh_weather = forced_all ||
                               now - last_weather >= pdMS_TO_TICKS(DATA_REFRESH_INTERVAL_MS);
        char claude_token[APP_CLAUDE_TOKEN_MAX + 1] = {0};
        if (s_config_mutex != NULL && xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (s_config.has_claude_token) {
                strlcpy(claude_token, s_config.claude_token, sizeof(claude_token));
            }
            xSemaphoreGive(s_config_mutex);
        }
        bool refresh_claude = claude_token[0] != '\0' &&
                              (forced_claude || last_claude == 0 ||
                               now - last_claude >= pdMS_TO_TICKS(DATA_REFRESH_INTERVAL_MS));
        if (!refresh_btc && !refresh_weather && !refresh_claude) {
            memset(claude_token, 0, sizeof(claude_token));
            continue;
        }

        app_ui_set_data_refreshing(true);
        if (refresh_btc) {
            app_btc_t btc = {0};
            char requested_currency[APP_CURRENCY_MAX + 1];
            strlcpy(requested_currency, s_config.btc_currency, sizeof(requested_currency));
            esp_err_t err = app_data_fetch_btc(requested_currency, &btc);
            if (err == ESP_OK && strcmp(btc.currency, s_config.btc_currency) == 0) {
                s_btc = btc;
                app_ui_set_internet_available(true);
                app_ui_update_btc(&s_btc);
                if (s_btc.point_count < 2) {
                    char message[128];
                    const char *detail = app_data_last_error();
                    snprintf(message, sizeof(message), "BTC graf: %s",
                             detail[0] ? detail : "bez dat");
                    app_ui_set_btc_status(message, true);
                }
                last_btc = now;
            } else if (err == ESP_OK) {
                ESP_LOGI(TAG, "Currency changed during update; discarding %s data", btc.currency);
                request_refresh();
            } else {
                ESP_LOGW(TAG, "BTC update failed: %s", esp_err_to_name(err));
                if (err == ESP_ERR_HTTP_CONNECT) app_ui_set_internet_available(false);
                char message[128];
                const char *detail = app_data_last_error();
                snprintf(message, sizeof(message), "BTC: %s",
                         detail[0] ? detail : esp_err_to_name(err));
                app_ui_set_btc_status(message, true);
            }
        }
        if (refresh_weather) {
            app_weather_t weather = {0};
            esp_err_t err = app_data_fetch_weather(s_config.latitude, s_config.longitude, &weather);
            if (err == ESP_OK) {
                s_weather = weather;
                app_ui_set_internet_available(true);
                app_ui_update_weather(&s_weather);
                last_weather = now;
            } else {
                ESP_LOGW(TAG, "Weather update failed: %s", esp_err_to_name(err));
                if (err == ESP_ERR_HTTP_CONNECT) app_ui_set_internet_available(false);
            }
        }
        if (refresh_claude) {
            app_claude_usage_t usage = {0};
            esp_err_t err = app_data_fetch_claude_usage(claude_token, &usage);
            memset(claude_token, 0, sizeof(claude_token));
            last_claude = now;
            if (err == ESP_OK) {
                s_claude = usage;
                app_ui_set_internet_available(true);
                app_ui_update_claude(&s_claude);
            } else {
                char message[128];
                const char *detail = app_data_last_error();
                snprintf(message, sizeof(message), "Claude: %s",
                         detail[0] ? detail : esp_err_to_name(err));
                app_ui_set_claude_status(message, true);
            }
        } else {
            memset(claude_token, 0, sizeof(claude_token));
        }
        app_ui_set_data_refreshing(false);
    }
}

static void request_refresh(void)
{
    if (s_refresh_task != NULL) xTaskNotify(s_refresh_task, REFRESH_ALL, eSetBits);
}

static void request_claude_refresh(void)
{
    if (s_refresh_task != NULL) xTaskNotify(s_refresh_task, REFRESH_CLAUDE, eSetBits);
}

static void web_claude_token_callback(const char *token, void *context)
{
    (void)context;
    bool configured = token != NULL && token[0] != '\0';
    if (s_config_mutex != NULL) xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    if (configured) {
        strlcpy(s_config.claude_token, token, sizeof(s_config.claude_token));
    } else {
        memset(s_config.claude_token, 0, sizeof(s_config.claude_token));
    }
    s_config.has_claude_token = configured;
    esp_err_t err = app_storage_save(&s_config);
    if (s_config_mutex != NULL) xSemaphoreGive(s_config_mutex);
    if (err != ESP_OK) ESP_LOGW(TAG, "Saving Claude token failed: %s", esp_err_to_name(err));
    if (!configured) memset(&s_claude, 0, sizeof(s_claude));
    app_web_set_token_configured(configured);
    app_ui_set_claude_configured(configured);
    if (configured) {
        app_ui_set_claude_status("Token uložen, načítám limity...", false);
        request_claude_refresh();
    }
}

static void network_callback(app_network_state_t state, const char *detail, void *context)
{
    (void)context;
    app_ui_set_network_state(state, detail);
    if (state == APP_NETWORK_CONNECTED) {
        if (s_credentials_pending) {
            esp_err_t err = app_storage_save(&s_config);
            if (err != ESP_OK) ESP_LOGW(TAG, "Saving Wi-Fi failed: %s", esp_err_to_name(err));
            s_credentials_pending = false;
        }
        start_time_sync();
        app_ui_set_device_address(detail);
        app_web_set_token_configured(s_config.has_claude_token);
        esp_err_t web_err = app_web_start(web_claude_token_callback, NULL);
        if (web_err != ESP_OK) ESP_LOGW(TAG, "Starting setup page failed: %s", esp_err_to_name(web_err));
        request_refresh();
    } else if (state == APP_NETWORK_DISCONNECTED || state == APP_NETWORK_FAILED) {
        app_web_stop();
        app_ui_set_device_address(NULL);
    }
}

static void scan_task(void *argument)
{
    (void)argument;
    app_wifi_ap_t results[APP_WIFI_AP_MAX] = {0};
    size_t count = 0;
    esp_err_t err = app_network_scan(results, APP_WIFI_AP_MAX, &count);
    if (err == ESP_OK) {
        app_ui_show_wifi_results(results, count, NULL);
    } else {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        app_ui_show_wifi_results(NULL, 0, "Hledání Wi-Fi se nezdařilo");
    }
    vTaskDelete(NULL);
}

static void ui_request_wifi_scan(void *context)
{
    (void)context;
    app_ui_show_wifi_scanning();
    xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 5, NULL);
}

static void connect_task(void *argument)
{
    connect_request_t *request = argument;
    strlcpy(s_config.wifi_ssid, request->ssid, sizeof(s_config.wifi_ssid));
    strlcpy(s_config.wifi_password, request->password, sizeof(s_config.wifi_password));
    s_config.has_wifi = true;
    s_credentials_pending = true;
    app_ui_show_dashboard(&s_config);
    app_ui_set_network_state(APP_NETWORK_CONNECTING, request->ssid);
    esp_err_t err = app_network_connect(request->ssid, request->password);
    if (err != ESP_OK) {
        app_ui_set_network_state(APP_NETWORK_FAILED, esp_err_to_name(err));
    }
    free(request);
    vTaskDelete(NULL);
}

static void ui_connect_wifi(const char *ssid, const char *password, void *context)
{
    (void)context;
    connect_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) return;
    strlcpy(request->ssid, ssid, sizeof(request->ssid));
    strlcpy(request->password, password, sizeof(request->password));
    xTaskCreate(connect_task, "wifi_connect", 4096, request, 5, NULL);
}

static void ui_forget_wifi(void *context)
{
    (void)context;
    app_network_disconnect();
    app_storage_forget_wifi(&s_config);
    app_ui_show_welcome();
}

static void geocode_task(void *argument)
{
    geocode_request_t *request = argument;
    app_geo_result_t results[APP_GEO_RESULTS_MAX] = {0};
    size_t count = 0;
    esp_err_t err = app_data_geocode(request->query, results, APP_GEO_RESULTS_MAX, &count);
    if (err == ESP_OK) {
        app_ui_show_geocode_results(results, count, NULL);
    } else {
        ESP_LOGW(TAG, "Geocoding failed: %s", esp_err_to_name(err));
        app_ui_show_geocode_results(NULL, 0, "Vyhledání se nezdařilo");
    }
    free(request);
    vTaskDelete(NULL);
}

static void ui_request_geocode(const char *query, void *context)
{
    (void)context;
    geocode_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) return;
    strlcpy(request->query, query, sizeof(request->query));
    xTaskCreate(geocode_task, "geocode", 6144, request, 5, NULL);
}

static void ui_select_location(const app_geo_result_t *location, void *context)
{
    (void)context;
    strlcpy(s_config.city, location->name, sizeof(s_config.city));
    s_config.latitude = location->latitude;
    s_config.longitude = location->longitude;
    app_storage_save(&s_config);
    memset(&s_weather, 0, sizeof(s_weather));
    app_ui_clear_weather_cache();
    app_ui_show_dashboard(&s_config);
    if (s_btc.valid) app_ui_update_btc(&s_btc);
    request_refresh();
}

static void ui_select_timezone(const char *name, const char *posix_tz, void *context)
{
    (void)context;
    strlcpy(s_config.timezone, name, sizeof(s_config.timezone));
    strlcpy(s_config.posix_tz, posix_tz, sizeof(s_config.posix_tz));
    setenv("TZ", s_config.posix_tz, 1);
    tzset();
    app_storage_save(&s_config);
    app_ui_show_dashboard(&s_config);
    if (s_weather.valid) app_ui_update_weather(&s_weather);
    if (s_btc.valid) app_ui_update_btc(&s_btc);
}

static void ui_select_currency(const char *currency, void *context)
{
    (void)context;
    if (currency == NULL || strcmp(currency, s_config.btc_currency) == 0) {
        app_ui_show_dashboard(&s_config);
        if (s_weather.valid) app_ui_update_weather(&s_weather);
        if (s_btc.valid) app_ui_update_btc(&s_btc);
        return;
    }
    strlcpy(s_config.btc_currency, currency, sizeof(s_config.btc_currency));
    app_storage_save(&s_config);
    memset(&s_btc, 0, sizeof(s_btc));
    app_ui_show_dashboard(&s_config);
    if (s_weather.valid) app_ui_update_weather(&s_weather);
    app_ui_set_btc_status("Načítám zvolenou měnu...", false);
    request_refresh();
}

static void ui_set_brightness(uint8_t brightness, void *context)
{
    (void)context;
    s_config.brightness = brightness;
    bsp_display_brightness_set(brightness);
    app_storage_save(&s_config);
}

static void ui_request_refresh(void *context)
{
    (void)context;
    request_refresh();
}

static void ui_request_claude_refresh(void *context)
{
    (void)context;
    request_claude_refresh();
}

static void ui_enable_claude_setup(void *context)
{
    (void)context;
    if (!app_network_is_connected()) {
        app_ui_set_claude_status("Nejdřív připojte Wi-Fi", true);
        return;
    }
    app_web_enable_claude_setup(10 * 60);
    app_ui_set_claude_status("Nastavení povoleno na 10 minut", false);
}

void app_main(void)
{
    ESP_ERROR_CHECK(app_storage_init());
    ESP_ERROR_CHECK(app_storage_load(&s_config));
    s_config_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_config_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    setenv("TZ", s_config.posix_tz, 1);
    tzset();

    ESP_LOGI(TAG, "Starting display");
    lv_display_t *display = bsp_display_start();
    ESP_ERROR_CHECK(display == NULL ? ESP_FAIL : ESP_OK);
    ESP_ERROR_CHECK(bsp_display_brightness_set(s_config.brightness));

    app_ui_callbacks_t callbacks = {
        .request_wifi_scan = ui_request_wifi_scan,
        .connect_wifi = ui_connect_wifi,
        .forget_wifi = ui_forget_wifi,
        .request_geocode = ui_request_geocode,
        .select_location = ui_select_location,
        .select_timezone = ui_select_timezone,
        .select_currency = ui_select_currency,
        .set_brightness = ui_set_brightness,
        .request_refresh = ui_request_refresh,
        .request_claude_refresh = ui_request_claude_refresh,
        .enable_claude_setup = ui_enable_claude_setup,
    };
    bsp_display_lock(0);
    app_ui_init(&s_config, &callbacks);
    bsp_display_unlock();

    ESP_ERROR_CHECK(app_network_init(network_callback, NULL));
    xTaskCreate(refresh_task, "data_refresh", 12288, NULL, 4, &s_refresh_task);

    if (s_config.has_wifi) {
        app_ui_show_dashboard(&s_config);
        app_network_connect(s_config.wifi_ssid, s_config.wifi_password);
    } else {
        app_ui_show_welcome();
    }
}
