#include "app_web.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "app_model.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "web_setup";
static httpd_handle_t s_server;
static app_web_claude_token_callback_t s_token_callback;
static void *s_callback_context;
static volatile int64_t s_setup_deadline_us;
static volatile bool s_token_configured;

static const char PAGE_HEAD[] =
    "<!doctype html><html lang='cs'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Krabička · Claude</title><style>"
    "body{margin:0;background:#07111f;color:#f4f7fb;font:16px system-ui,sans-serif}"
    ".box{max-width:560px;margin:32px auto;padding:24px}"
    ".card{background:#111d2e;border:1px solid #22324a;border-radius:20px;padding:24px}"
    "h1{margin:0 0 10px}p{color:#a9b7ca;line-height:1.5}code{color:#fbbf24}"
    "input{box-sizing:border-box;width:100%;padding:14px;border-radius:12px;border:1px solid #38506f;"
    "background:#07111f;color:white;font-size:16px}button{margin-top:14px;padding:13px 18px;"
    "border:0;border-radius:12px;background:#0284c7;color:white;font-weight:700;font-size:16px}"
    ".danger{background:#7f1d1d}.ok{color:#34d399}.warn{color:#fbbf24}</style></head><body>"
    "<main class='box'><section class='card'><h1>Claude limity</h1>";

static const char PAGE_FOOT[] = "</section></main></body></html>";

bool app_web_claude_setup_enabled(void)
{
    return s_setup_deadline_us > esp_timer_get_time();
}

static esp_err_t send_page(httpd_req_t *request, const char *body)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_send_chunk(request, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(request, body, HTTPD_RESP_USE_STRLEN);
    esp_err_t err = httpd_resp_send_chunk(request, PAGE_FOOT, HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) return err;
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t root_handler(httpd_req_t *request)
{
    if (!app_web_claude_setup_enabled()) {
        return send_page(request,
            "<p class='warn'>Nastavení je zamčené.</p>"
            "<p>Na displeji otevřete <b>Nastavení → Claude limity</b> a klepněte na "
            "<b>Povolit nastavení</b>. Přístup se otevře na 10 minut.</p>");
    }

    const char *configured = s_token_configured
        ? "<p class='ok'>Token je nyní nastavený. Vložením nového jej nahradíte.</p>"
        : "<p>Token zatím není nastavený.</p>";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_send_chunk(request, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(request, configured, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(request,
        "<p>Na počítači spusťte <code>claude setup-token</code> a výsledek vložte sem. "
        "Krabička token nikdy nezobrazí zpět.</p>"
        "<form method='post' action='/claude-token'>"
        "<input type='password' name='token' autocomplete='off' maxlength='383' "
        "placeholder='Setup token' required><button type='submit'>Uložit token</button></form>"
        "<form method='post' action='/claude-remove'>"
        "<button class='danger' type='submit'>Odstranit token</button></form>",
        HTTPD_RESP_USE_STRLEN);
    esp_err_t err = httpd_resp_send_chunk(request, PAGE_FOOT, HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) return err;
    return httpd_resp_send_chunk(request, NULL, 0);
}

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') return character - '0';
    character = (char)tolower((unsigned char)character);
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    return -1;
}

static void url_decode(char *text)
{
    char *source = text;
    char *destination = text;
    while (*source) {
        if (*source == '+' ) {
            *destination++ = ' ';
            source++;
        } else if (*source == '%' && source[1] && source[2]) {
            int high = hex_value(source[1]);
            int low = hex_value(source[2]);
            if (high >= 0 && low >= 0) {
                *destination++ = (char)((high << 4) | low);
                source += 3;
            } else {
                *destination++ = *source++;
            }
        } else {
            *destination++ = *source++;
        }
    }
    *destination = '\0';
}

static esp_err_t receive_body(httpd_req_t *request, char **result)
{
    if (request->content_len <= 0 || request->content_len > APP_CLAUDE_TOKEN_MAX * 3 + 16) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *body = calloc(1, request->content_len + 1);
    if (body == NULL) return ESP_ERR_NO_MEM;
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received,
                                   request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (count <= 0) {
            free(body);
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    *result = body;
    return ESP_OK;
}

static esp_err_t token_handler(httpd_req_t *request)
{
    if (!app_web_claude_setup_enabled()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Nastaveni neni povoleno na displeji");
    }
    char *body = NULL;
    esp_err_t err = receive_body(request, &body);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Neplatny pozadavek");
    }
    char *token = strstr(body, "token=");
    if (token == NULL) {
        free(body);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Token chybi");
    }
    token += strlen("token=");
    char *separator = strchr(token, '&');
    if (separator != NULL) *separator = '\0';
    url_decode(token);
    while (isspace((unsigned char)*token)) token++;
    char *end = token + strlen(token);
    while (end > token && isspace((unsigned char)end[-1])) *--end = '\0';
    if (token[0] == '\0' || strlen(token) > APP_CLAUDE_TOKEN_MAX) {
        memset(body, 0, request->content_len);
        free(body);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Token ma spatnou delku");
    }
    if (s_token_callback != NULL) s_token_callback(token, s_callback_context);
    memset(body, 0, request->content_len);
    free(body);
    s_token_configured = true;
    s_setup_deadline_us = 0;
    ESP_LOGI(TAG, "Claude token updated from local setup page");
    return send_page(request,
        "<p class='ok'><b>Hotovo.</b> Token je uložený a krabička načítá limity.</p>"
        "<p>Tuto stránku můžete zavřít.</p>");
}

static esp_err_t remove_handler(httpd_req_t *request)
{
    if (!app_web_claude_setup_enabled()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Nastaveni neni povoleno na displeji");
    }
    if (s_token_callback != NULL) s_token_callback("", s_callback_context);
    s_token_configured = false;
    s_setup_deadline_us = 0;
    ESP_LOGI(TAG, "Claude token removed from local setup page");
    return send_page(request,
        "<p class='ok'><b>Hotovo.</b> Token byl z krabičky odstraněn.</p>");
}

esp_err_t app_web_start(app_web_claude_token_callback_t callback, void *context)
{
    s_token_callback = callback;
    s_callback_context = context;
    if (s_server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;

    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    const httpd_uri_t token = {.uri = "/claude-token", .method = HTTP_POST, .handler = token_handler};
    const httpd_uri_t remove = {.uri = "/claude-remove", .method = HTTP_POST, .handler = remove_handler};
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &token);
    httpd_register_uri_handler(s_server, &remove);
    ESP_LOGI(TAG, "Local setup page started");
    return ESP_OK;
}

void app_web_stop(void)
{
    if (s_server == NULL) return;
    httpd_stop(s_server);
    s_server = NULL;
    s_setup_deadline_us = 0;
}

void app_web_enable_claude_setup(uint32_t seconds)
{
    s_setup_deadline_us = esp_timer_get_time() + (int64_t)seconds * 1000000LL;
}

void app_web_set_token_configured(bool configured)
{
    s_token_configured = configured;
}
