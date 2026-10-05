#include "app_storage.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "storage";
static const char *NAMESPACE = "dashboard";

void app_config_set_defaults(app_config_t *config)
{
    memset(config, 0, sizeof(*config));
    strlcpy(config->city, "Praha", sizeof(config->city));
    config->latitude = 50.0755;
    config->longitude = 14.4378;
    strlcpy(config->timezone, "Europe/Prague", sizeof(config->timezone));
    strlcpy(config->posix_tz, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(config->posix_tz));
    strlcpy(config->btc_currency, "USD", sizeof(config->btc_currency));
    config->brightness = 70;
}

esp_err_t app_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void get_string(nvs_handle_t handle, const char *key, char *value, size_t size)
{
    size_t required = size;
    if (nvs_get_str(handle, key, value, &required) != ESP_OK) {
        value[0] = '\0';
    }
}

esp_err_t app_storage_load(app_config_t *config)
{
    app_config_set_defaults(config);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t has_wifi = 0;
    nvs_get_u8(handle, "has_wifi", &has_wifi);
    config->has_wifi = has_wifi == 1;
    get_string(handle, "ssid", config->wifi_ssid, sizeof(config->wifi_ssid));
    get_string(handle, "password", config->wifi_password, sizeof(config->wifi_password));
    get_string(handle, "city", config->city, sizeof(config->city));
    get_string(handle, "timezone", config->timezone, sizeof(config->timezone));
    get_string(handle, "posix_tz", config->posix_tz, sizeof(config->posix_tz));
    get_string(handle, "btc_ccy", config->btc_currency, sizeof(config->btc_currency));
    get_string(handle, "claude_token", config->claude_token, sizeof(config->claude_token));
    config->has_claude_token = config->claude_token[0] != '\0';

    int64_t latitude_e6 = 0;
    int64_t longitude_e6 = 0;
    if (nvs_get_i64(handle, "latitude", &latitude_e6) == ESP_OK) {
        config->latitude = latitude_e6 / 1000000.0;
    }
    if (nvs_get_i64(handle, "longitude", &longitude_e6) == ESP_OK) {
        config->longitude = longitude_e6 / 1000000.0;
    }
    nvs_get_u8(handle, "brightness", &config->brightness);
    nvs_close(handle);

    if (config->city[0] == '\0') {
        strlcpy(config->city, "Praha", sizeof(config->city));
    }
    if (config->timezone[0] == '\0') {
        strlcpy(config->timezone, "Europe/Prague", sizeof(config->timezone));
    }
    if (config->posix_tz[0] == '\0') {
        strlcpy(config->posix_tz, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(config->posix_tz));
    }
    if (strcmp(config->btc_currency, "USD") != 0 &&
        strcmp(config->btc_currency, "EUR") != 0 &&
        strcmp(config->btc_currency, "CZK") != 0) {
        strlcpy(config->btc_currency, "USD", sizeof(config->btc_currency));
    }
    if (config->brightness < 10 || config->brightness > 100) {
        config->brightness = 70;
    }

    ESP_LOGI(TAG, "Loaded config: city=%s timezone=%s wifi=%s",
             config->city, config->timezone, config->has_wifi ? config->wifi_ssid : "not set");
    return ESP_OK;
}

esp_err_t app_storage_save(const app_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, "has_wifi", config->has_wifi ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, "ssid", config->wifi_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "password", config->wifi_password);
    if (err == ESP_OK) err = nvs_set_str(handle, "city", config->city);
    if (err == ESP_OK) err = nvs_set_str(handle, "timezone", config->timezone);
    if (err == ESP_OK) err = nvs_set_str(handle, "posix_tz", config->posix_tz);
    if (err == ESP_OK) err = nvs_set_str(handle, "btc_ccy", config->btc_currency);
    if (err == ESP_OK) err = nvs_set_str(handle, "claude_token", config->claude_token);
    if (err == ESP_OK) err = nvs_set_i64(handle, "latitude", (int64_t)(config->latitude * 1000000.0));
    if (err == ESP_OK) err = nvs_set_i64(handle, "longitude", (int64_t)(config->longitude * 1000000.0));
    if (err == ESP_OK) err = nvs_set_u8(handle, "brightness", config->brightness);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t app_storage_forget_wifi(app_config_t *config)
{
    config->has_wifi = false;
    config->wifi_ssid[0] = '\0';
    config->wifi_password[0] = '\0';
    return app_storage_save(config);
}
