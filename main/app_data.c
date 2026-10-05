#include "app_data.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "data";
#define HTTP_RESPONSE_LIMIT (64 * 1024)
static char s_last_error[96];

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} http_buffer_t;

typedef struct {
    char five_hour_utilization[32];
    char five_hour_reset[32];
    char seven_day_utilization[32];
    char seven_day_reset[32];
} claude_headers_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    http_buffer_t *buffer = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || buffer == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    size_t needed = buffer->length + event->data_len + 1;
    if (needed > HTTP_RESPONSE_LIMIT) {
        buffer->failed = true;
        return ESP_ERR_NO_MEM;
    }
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 2048 : buffer->capacity;
        while (capacity < needed) {
            capacity *= 2;
        }
        char *expanded = realloc(buffer->data, capacity);
        if (expanded == NULL) {
            buffer->failed = true;
            return ESP_ERR_NO_MEM;
        }
        buffer->data = expanded;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, event->data, event->data_len);
    buffer->length += event->data_len;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

static void copy_header_value(char *destination, size_t capacity,
                              const esp_http_client_event_t *event)
{
    if (event->header_value == NULL || capacity == 0) return;
    strlcpy(destination, event->header_value, capacity);
}

static esp_err_t claude_http_event(esp_http_client_event_t *event)
{
    claude_headers_t *headers = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_HEADER || headers == NULL ||
        event->header_key == NULL) {
        return ESP_OK;
    }

    if (strcasecmp(event->header_key, "anthropic-ratelimit-unified-5h-utilization") == 0) {
        copy_header_value(headers->five_hour_utilization,
                          sizeof(headers->five_hour_utilization), event);
    } else if (strcasecmp(event->header_key, "anthropic-ratelimit-unified-5h-reset") == 0) {
        copy_header_value(headers->five_hour_reset, sizeof(headers->five_hour_reset), event);
    } else if (strcasecmp(event->header_key, "anthropic-ratelimit-unified-7d-utilization") == 0) {
        copy_header_value(headers->seven_day_utilization,
                          sizeof(headers->seven_day_utilization), event);
    } else if (strcasecmp(event->header_key, "anthropic-ratelimit-unified-7d-reset") == 0) {
        copy_header_value(headers->seven_day_reset, sizeof(headers->seven_day_reset), event);
    }
    return ESP_OK;
}

static bool parse_float_header(const char *text, float *value)
{
    if (text == NULL || text[0] == '\0') return false;
    char *end = NULL;
    float parsed = strtof(text, &end);
    if (end == text || !isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

static time_t parse_epoch_header(const char *text)
{
    if (text == NULL || text[0] == '\0') return 0;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);
    return end == text || parsed <= 0 ? 0 : (time_t)parsed;
}

esp_err_t app_data_fetch_claude_usage(const char *setup_token, app_claude_usage_t *usage)
{
    s_last_error[0] = '\0';
    if (setup_token == NULL || setup_token[0] == '\0' || usage == NULL) {
        snprintf(s_last_error, sizeof(s_last_error), "chybí setup token");
        return ESP_ERR_INVALID_ARG;
    }

    static const char body[] =
        "{\"model\":\"claude-haiku-4-5-20251001\",\"max_tokens\":1,"
        "\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}";
    claude_headers_t headers = {0};
    esp_http_client_config_t config = {
        .url = "https://api.anthropic.com/v1/messages",
        .method = HTTP_METHOD_POST,
        .event_handler = claude_http_event,
        .user_data = &headers,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;

    char authorization[APP_CLAUDE_TOKEN_MAX + 16];
    snprintf(authorization, sizeof(authorization), "Bearer %s", setup_token);
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "anthropic-version", "2023-06-01");
    esp_http_client_set_header(client, "anthropic-beta", "oauth-2025-04-20");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", "krabicka-dashboard/0.3");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int socket_errno = esp_http_client_get_errno(client);
    int tls_error = 0;
    int tls_flags = 0;
    esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &tls_flags);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));

    float five_hour = 0.0f;
    float seven_day = 0.0f;
    bool have_usage = parse_float_header(headers.five_hour_utilization, &five_hour) &&
                      parse_float_header(headers.seven_day_utilization, &seven_day);
    /* A 429 response still carries the useful quota headers. */
    if (err != ESP_OK || (!have_usage && (status < 200 || status >= 300))) {
        if (status == 401 || status == 403) {
            snprintf(s_last_error, sizeof(s_last_error), "token je neplatný nebo vypršel");
        } else if (status > 0) {
            snprintf(s_last_error, sizeof(s_last_error), "Anthropic HTTP %d", status);
        } else if (tls_error != 0) {
            snprintf(s_last_error, sizeof(s_last_error), "TLS 0x%04X (flags 0x%X)",
                     (unsigned)(tls_error < 0 ? -tls_error : tls_error),
                     (unsigned)tls_flags);
        } else if (socket_errno != 0) {
            snprintf(s_last_error, sizeof(s_last_error), "síťová chyba %d", socket_errno);
        } else {
            snprintf(s_last_error, sizeof(s_last_error), "%s", esp_err_to_name(err));
        }
        ESP_LOGW(TAG, "Claude usage failed: err=%s status=%d", esp_err_to_name(err), status);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    if (!have_usage) {
        snprintf(s_last_error, sizeof(s_last_error), "odpověď neobsahuje limity");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Anthropic sends utilization as 0..1. Be liberal if that ever changes. */
    if (five_hour <= 1.001f) five_hour *= 100.0f;
    if (seven_day <= 1.001f) seven_day *= 100.0f;
    app_claude_usage_t parsed = {
        .valid = true,
        .five_hour_used_percent = fminf(fmaxf(five_hour, 0.0f), 100.0f),
        .seven_day_used_percent = fminf(fmaxf(seven_day, 0.0f), 100.0f),
        .five_hour_reset = parse_epoch_header(headers.five_hour_reset),
        .seven_day_reset = parse_epoch_header(headers.seven_day_reset),
        .updated_at = time(NULL),
    };
    *usage = parsed;
    ESP_LOGI(TAG, "Claude limits updated: 5h %.1f%% used, 7d %.1f%% used",
             parsed.five_hour_used_percent, parsed.seven_day_used_percent);
    return ESP_OK;
}

static esp_err_t http_get_json(const char *url, char **response)
{
    http_buffer_t buffer = {0};
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event,
        .user_data = &buffer,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "User-Agent", "krabicka-dashboard/0.2");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int socket_errno = esp_http_client_get_errno(client);
    int tls_error = 0;
    int tls_flags = 0;
    esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &tls_flags);
    esp_http_client_cleanup(client);

    /* Some Cloudflare edges close an otherwise complete chunked response
     * without the final zero-length chunk.  Accept it only when the body has
     * a complete JSON closing delimiter and HTTP itself succeeded. */
    bool complete_json_body = false;
    if (buffer.data != NULL && buffer.length > 0) {
        size_t tail = buffer.length;
        while (tail > 0 && isspace((unsigned char)buffer.data[tail - 1])) --tail;
        complete_json_body = tail > 0 &&
                             (buffer.data[tail - 1] == ']' || buffer.data[tail - 1] == '}');
    }
    if (err == ESP_ERR_HTTP_INCOMPLETE_DATA && status >= 200 && status < 300 && complete_json_body) {
        ESP_LOGW(TAG, "Accepting complete JSON after incomplete chunk terminator: %s", url);
        err = ESP_OK;
    }
    if (err != ESP_OK || buffer.failed || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "GET failed: err=%s status=%d url=%s", esp_err_to_name(err), status, url);
        if (buffer.failed) {
            snprintf(s_last_error, sizeof(s_last_error), "odpověď je příliš velká");
        } else if (status > 0) {
            snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", status);
        } else if (tls_error != 0) {
            snprintf(s_last_error, sizeof(s_last_error), "TLS 0x%04X (flags 0x%X)",
                     (unsigned)(tls_error < 0 ? -tls_error : tls_error), (unsigned)tls_flags);
        } else if (socket_errno != 0) {
            snprintf(s_last_error, sizeof(s_last_error), "síťová chyba %d", socket_errno);
        } else {
            snprintf(s_last_error, sizeof(s_last_error), "%s", esp_err_to_name(err));
        }
        free(buffer.data);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    if (buffer.data == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *response = buffer.data;
    return ESP_OK;
}

static bool json_number(cJSON *object, const char *name, double *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *value = item->valuedouble;
    return true;
}

static size_t json_number_array(cJSON *object, const char *name, double *values, size_t capacity)
{
    cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsArray(array)) {
        return 0;
    }
    size_t count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, array) {
        if (count >= capacity) break;
        if (cJSON_IsNumber(item)) values[count++] = item->valuedouble;
    }
    return count;
}

esp_err_t app_data_fetch_weather(double latitude, double longitude, app_weather_t *weather)
{
    char url[768];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
             "&current=temperature_2m,apparent_temperature,precipitation_probability,weather_code"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&forecast_days=5&timezone=auto",
             latitude, longitude);

    char *response = NULL;
    esp_err_t err = http_get_json(url, &response);
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(response);
    free(response);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;

    app_weather_t parsed = {0};
    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    double code = 0;
    double precipitation = 0;
    bool valid = cJSON_IsObject(current) &&
                 json_number(current, "temperature_2m", &parsed.temperature) &&
                 json_number(current, "apparent_temperature", &parsed.apparent_temperature) &&
                 json_number(current, "weather_code", &code);
    json_number(current, "precipitation_probability", &precipitation);
    parsed.weather_code = (int)code;
    parsed.precipitation_probability = (int)precipitation;

    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    double daily_codes[APP_WEATHER_DAYS] = {0};
    double daily_precip[APP_WEATHER_DAYS] = {0};
    size_t code_count = json_number_array(daily, "weather_code", daily_codes, APP_WEATHER_DAYS);
    size_t min_count = json_number_array(daily, "temperature_2m_min", parsed.daily_min, APP_WEATHER_DAYS);
    size_t max_count = json_number_array(daily, "temperature_2m_max", parsed.daily_max, APP_WEATHER_DAYS);
    size_t precip_count = json_number_array(daily, "precipitation_probability_max", daily_precip, APP_WEATHER_DAYS);
    parsed.daily_count = code_count;
    if (parsed.daily_count > min_count) parsed.daily_count = min_count;
    if (parsed.daily_count > max_count) parsed.daily_count = max_count;
    for (size_t i = 0; i < parsed.daily_count; ++i) {
        parsed.daily_code[i] = (int)daily_codes[i];
        parsed.daily_precipitation[i] = i < precip_count ? (int)daily_precip[i] : 0;
    }
    if (parsed.daily_count > 0) {
        parsed.temperature_min = parsed.daily_min[0];
        parsed.temperature_max = parsed.daily_max[0];
    }
    parsed.valid = valid;
    parsed.updated_at = time(NULL);
    cJSON_Delete(root);

    if (!valid) return ESP_ERR_INVALID_RESPONSE;
    *weather = parsed;
    return ESP_OK;
}

static bool json_string_number(cJSON *object, const char *name, double *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) return false;
    char *end = NULL;
    double parsed = strtod(item->valuestring, &end);
    if (end == item->valuestring) return false;
    *value = parsed;
    return true;
}

esp_err_t app_data_fetch_btc(const char *currency, app_btc_t *btc)
{
    s_last_error[0] = '\0';
    if (currency == NULL ||
        (strcmp(currency, "USD") != 0 && strcmp(currency, "EUR") != 0 &&
         strcmp(currency, "CZK") != 0)) {
        currency = "USD";
    }
    const char *market_currency = strcmp(currency, "EUR") == 0 ? "EUR" : "USD";
    char ticker_url[128];
    snprintf(ticker_url, sizeof(ticker_url),
             "https://api.exchange.coinbase.com/products/BTC-%s/ticker",
             market_currency);
    char *ticker_response = NULL;
    esp_err_t err = http_get_json(ticker_url, &ticker_response);
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(ticker_response);
    free(ticker_response);
    if (root == NULL) {
        snprintf(s_last_error, sizeof(s_last_error), "neplatná odpověď ceny");
        return ESP_ERR_INVALID_RESPONSE;
    }

    app_btc_t parsed = {0};
    json_string_number(root, "price", &parsed.price);
    cJSON_Delete(root);
    if (parsed.price <= 0) {
        snprintf(s_last_error, sizeof(s_last_error), "v odpovědi chybí cena");
        return ESP_ERR_INVALID_RESPONSE;
    }
    strlcpy(parsed.currency, currency, sizeof(parsed.currency));

    double conversion = 1.0;
    if (strcmp(currency, "CZK") == 0) {
        double usd_price = parsed.price;
        char *spot_response = NULL;
        err = http_get_json("https://api.coinbase.com/v2/prices/BTC-CZK/spot",
                            &spot_response);
        if (err != ESP_OK) return err;
        root = cJSON_Parse(spot_response);
        free(spot_response);
        cJSON *data = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
        double czk_price = 0;
        bool spot_valid = cJSON_IsObject(data) &&
                          json_string_number(data, "amount", &czk_price) &&
                          czk_price > 0;
        cJSON_Delete(root);
        if (!spot_valid) {
            snprintf(s_last_error, sizeof(s_last_error), "neplatná cena v CZK");
            return ESP_ERR_INVALID_RESPONSE;
        }
        conversion = czk_price / usd_price;
        parsed.price = czk_price;
    }

    time_t end = time(NULL);
    time_t start = end - 24 * 60 * 60;
    struct tm start_utc;
    struct tm end_utc;
    gmtime_r(&start, &start_utc);
    gmtime_r(&end, &end_utc);
    char start_text[24];
    char end_text[24];
    strftime(start_text, sizeof(start_text), "%Y-%m-%dT%H:%M:%SZ", &start_utc);
    strftime(end_text, sizeof(end_text), "%Y-%m-%dT%H:%M:%SZ", &end_utc);
    char candles_url[256];
    snprintf(candles_url, sizeof(candles_url),
             "https://api.exchange.coinbase.com/products/BTC-%s/candles"
             "?granularity=3600&start=%s&end=%s",
             market_currency, start_text, end_text);
    char *candles_response = NULL;
    err = http_get_json(candles_url, &candles_response);
    if (err == ESP_OK) {
        root = cJSON_Parse(candles_response);
        free(candles_response);
        if (cJSON_IsArray(root)) {
            int total = cJSON_GetArraySize(root);
            int count = total > APP_BTC_POINTS_MAX ? APP_BTC_POINTS_MAX : total;
            double open_24h = 0;

            /* Coinbase returns newest candle first; the chart expects oldest
             * first, so walk the selected range backwards. */
            for (int i = count - 1; i >= 0; --i) {
                cJSON *candle = cJSON_GetArrayItem(root, i);
                cJSON *low = cJSON_IsArray(candle) ? cJSON_GetArrayItem(candle, 1) : NULL;
                cJSON *high = cJSON_IsArray(candle) ? cJSON_GetArrayItem(candle, 2) : NULL;
                cJSON *open = cJSON_IsArray(candle) ? cJSON_GetArrayItem(candle, 3) : NULL;
                cJSON *close = cJSON_IsArray(candle) ? cJSON_GetArrayItem(candle, 4) : NULL;
                if (i == count - 1) {
                    if (cJSON_IsNumber(open)) open_24h = open->valuedouble * conversion;
                }
                if (cJSON_IsNumber(open) && cJSON_IsNumber(high) &&
                    cJSON_IsNumber(low) && cJSON_IsNumber(close) &&
                    open->valuedouble > 0 && high->valuedouble > 0 &&
                    low->valuedouble > 0 && close->valuedouble > 0) {
                    app_btc_candle_t *point = &parsed.candles[parsed.point_count++];
                    point->open = (float)(open->valuedouble * conversion);
                    point->high = (float)(high->valuedouble * conversion);
                    point->low = (float)(low->valuedouble * conversion);
                    point->close = (float)(close->valuedouble * conversion);
                }
            }
            if (open_24h > 0) {
                parsed.change_percent_24h =
                    (parsed.price - open_24h) / open_24h * 100.0;
            }
            if (parsed.point_count == 0) {
                snprintf(s_last_error, sizeof(s_last_error), "svíčky neobsahují data");
            }
        } else {
            snprintf(s_last_error, sizeof(s_last_error), "neplatná odpověď grafu");
        }
        cJSON_Delete(root);
    }

    parsed.valid = true;
    parsed.updated_at = time(NULL);
    ESP_LOGI(TAG, "BTC updated: price=%.2f %s change=%+.2f%% points=%u",
             parsed.price, parsed.currency, parsed.change_percent_24h,
             (unsigned)parsed.point_count);
    *btc = parsed;
    return ESP_OK;
}

const char *app_data_last_error(void)
{
    return s_last_error;
}

static bool is_unreserved(unsigned char character)
{
    return isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~';
}

static bool url_encode(const char *input, char *output, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    while (*input) {
        unsigned char value = (unsigned char)*input++;
        if (is_unreserved(value)) {
            if (used + 1 >= capacity) return false;
            output[used++] = value;
        } else {
            if (used + 3 >= capacity) return false;
            output[used++] = '%';
            output[used++] = hex[value >> 4];
            output[used++] = hex[value & 0x0F];
        }
    }
    output[used] = '\0';
    return true;
}

esp_err_t app_data_geocode(const char *query, app_geo_result_t *results, size_t capacity, size_t *result_count)
{
    if (query == NULL || query[0] == '\0' || results == NULL || capacity == 0 || result_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char encoded[192];
    if (!url_encode(query, encoded, sizeof(encoded))) return ESP_ERR_INVALID_SIZE;

    char url[384];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=5&language=cs&format=json",
             encoded);
    char *response = NULL;
    esp_err_t err = http_get_json(url, &response);
    if (err != ESP_OK) return err;
    cJSON *root = cJSON_Parse(response);
    free(response);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;

    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "results");
    size_t count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, array) {
        if (count >= capacity) break;
        cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        cJSON *country = cJSON_GetObjectItemCaseSensitive(item, "country");
        cJSON *timezone = cJSON_GetObjectItemCaseSensitive(item, "timezone");
        cJSON *latitude = cJSON_GetObjectItemCaseSensitive(item, "latitude");
        cJSON *longitude = cJSON_GetObjectItemCaseSensitive(item, "longitude");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(latitude) || !cJSON_IsNumber(longitude)) continue;
        strlcpy(results[count].name, name->valuestring, sizeof(results[count].name));
        if (cJSON_IsString(country)) strlcpy(results[count].country, country->valuestring, sizeof(results[count].country));
        if (cJSON_IsString(timezone)) strlcpy(results[count].timezone, timezone->valuestring, sizeof(results[count].timezone));
        results[count].latitude = latitude->valuedouble;
        results[count].longitude = longitude->valuedouble;
        count++;
    }
    cJSON_Delete(root);
    *result_count = count;
    return ESP_OK;
}

const char *app_weather_description(int code)
{
    if (code == 0) return "Jasno";
    if (code <= 2) return "Polojasno";
    if (code == 3) return "Zataženo";
    if (code == 45 || code == 48) return "Mlha";
    if (code >= 51 && code <= 57) return "Mrholení";
    if (code >= 61 && code <= 67) return "Déšť";
    if (code >= 71 && code <= 77) return "Sněžení";
    if (code >= 80 && code <= 82) return "Přeháňky";
    if (code >= 85 && code <= 86) return "Sněhové přeháňky";
    if (code >= 95) return "Bouřka";
    return "Neznámé";
}
