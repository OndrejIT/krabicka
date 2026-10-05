#include "app_network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "network";
static const EventBits_t CONNECTED_BIT = BIT0;

static EventGroupHandle_t s_event_group;
static app_network_callback_t s_callback;
static void *s_callback_context;
static bool s_started;
static bool s_manual_disconnect;
static unsigned s_retry_count;

static void notify(app_network_state_t state, const char *detail)
{
    if (s_callback != NULL) {
        s_callback(state, detail, s_callback_context);
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_event_group, CONNECTED_BIT);
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Disconnected, reason=%d", event ? event->reason : -1);

        if (s_manual_disconnect) {
            s_manual_disconnect = false;
            notify(APP_NETWORK_DISCONNECTED, "Odpojeno");
        } else if (s_retry_count < 8) {
            s_retry_count++;
            notify(APP_NETWORK_CONNECTING, "Opakuji připojení...");
            esp_wifi_connect();
        } else {
            notify(APP_NETWORK_FAILED, "Připojení se nezdařilo");
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        char address[20] = {0};
        if (event != NULL) {
            snprintf(address, sizeof(address), IPSTR, IP2STR(&event->ip_info.ip));
        }
        s_retry_count = 0;
        xEventGroupSetBits(s_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected, IP=%s", address);
        notify(APP_NETWORK_CONNECTED, address);
    }
}

esp_err_t app_network_init(app_network_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_context = context;
    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        return loop_err;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL), TAG, "ip handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    s_started = true;
    return ESP_OK;
}

esp_err_t app_network_connect(const char *ssid, const char *password)
{
    if (!s_started || ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password ? password : "", sizeof(config.sta.password));
    config.sta.threshold.authmode = password && password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    /* On a cold start there is no old connection to disconnect. Marking the
     * first real failure as a manual disconnect used to suppress all retries. */
    bool was_connected = app_network_is_connected();
    s_manual_disconnect = was_connected;
    if (was_connected) {
        esp_err_t disconnect_err = esp_wifi_disconnect();
        if (disconnect_err == ESP_ERR_WIFI_NOT_CONNECT) s_manual_disconnect = false;
    }
    s_retry_count = 0;
    xEventGroupClearBits(s_event_group, CONNECTED_BIT);

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "wifi config");
    notify(APP_NETWORK_CONNECTING, ssid);
    return esp_wifi_connect();
}

esp_err_t app_network_disconnect(void)
{
    s_manual_disconnect = true;
    xEventGroupClearBits(s_event_group, CONNECTED_BIT);
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_CONNECT) s_manual_disconnect = false;
    return err;
}

bool app_network_is_connected(void)
{
    return s_event_group != NULL && (xEventGroupGetBits(s_event_group) & CONNECTED_BIT) != 0;
}

static int compare_ap(const void *left, const void *right)
{
    const app_wifi_ap_t *a = left;
    const app_wifi_ap_t *b = right;
    return b->rssi - a->rssi;
}

esp_err_t app_network_scan(app_wifi_ap_t *results, size_t capacity, size_t *result_count)
{
    if (!s_started || results == NULL || result_count == NULL || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_scan_config_t scan = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan, true), TAG, "wifi scan");

    uint16_t count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&count), TAG, "scan count");
    if (count == 0) {
        *result_count = 0;
        return ESP_OK;
    }

    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    if (records == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        free(records);
        return err;
    }

    size_t output = 0;
    for (uint16_t i = 0; i < count && output < capacity; ++i) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        bool duplicate = false;
        for (size_t j = 0; j < output; ++j) {
            if (strcmp(results[j].ssid, (char *)records[i].ssid) == 0) {
                duplicate = true;
                if (records[i].rssi > results[j].rssi) {
                    results[j].rssi = records[i].rssi;
                }
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        strlcpy(results[output].ssid, (char *)records[i].ssid, sizeof(results[output].ssid));
        results[output].rssi = records[i].rssi;
        results[output].secured = records[i].authmode != WIFI_AUTH_OPEN;
        output++;
    }
    free(records);
    qsort(results, output, sizeof(results[0]), compare_ap);
    *result_count = output;
    return ESP_OK;
}
