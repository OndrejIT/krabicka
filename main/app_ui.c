#include "app_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_data.h"
#include "app_fonts.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "lvgl.h"

#define COLOR_BG 0x07111F
#define COLOR_CARD 0x111D2E
#define COLOR_CARD_ALT 0x16243A
#define COLOR_TEXT 0xF4F7FB
#define COLOR_MUTED 0x94A3B8
#define COLOR_BLUE 0x38BDF8
#define COLOR_GREEN 0x34D399
#define COLOR_RED 0xFB7185
#define COLOR_YELLOW 0xFBBF24

static const char *TAG = "ui";

static app_ui_callbacks_t s_callbacks;
static app_config_t s_config;
static app_wifi_ap_t s_aps[APP_WIFI_AP_MAX];
static size_t s_ap_count;
static app_geo_result_t s_geo[APP_GEO_RESULTS_MAX];
static size_t s_geo_count;
static char s_selected_ssid[APP_WIFI_SSID_MAX + 1];
static app_weather_t s_cached_weather;
static app_btc_t s_cached_btc;
static app_claude_usage_t s_cached_claude;
static char s_device_address[48];
static bool s_on_claude_screen;
static bool s_data_refreshing;

static lv_obj_t *s_clock_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_location_label;
static lv_obj_t *s_network_label;
static lv_obj_t *s_weather_temp;
static lv_obj_t *s_weather_icon;
static lv_obj_t *s_weather_description;
static lv_obj_t *s_weather_detail;
static lv_obj_t *s_btc_price;
static lv_obj_t *s_btc_change;
static lv_obj_t *s_btc_chart;
static lv_obj_t *s_updated_label;
static lv_obj_t *s_wifi_status;
static lv_obj_t *s_password_textarea;
static lv_obj_t *s_location_textarea;
static lv_obj_t *s_forecast_icon[APP_WEATHER_DAYS];
static lv_obj_t *s_forecast_description[APP_WEATHER_DAYS];
static lv_obj_t *s_forecast_temperature[APP_WEATHER_DAYS];
static lv_obj_t *s_forecast_rain[APP_WEATHER_DAYS];
static lv_obj_t *s_forecast_updated;
static lv_obj_t *s_claude_5h_bar;
static lv_obj_t *s_claude_7d_bar;
static lv_obj_t *s_claude_5h_value;
static lv_obj_t *s_claude_7d_value;
static lv_obj_t *s_claude_5h_reset_compact;
static lv_obj_t *s_claude_detail_5h_bar;
static lv_obj_t *s_claude_detail_7d_bar;
static lv_obj_t *s_claude_detail_5h_value;
static lv_obj_t *s_claude_detail_7d_value;
static lv_obj_t *s_claude_detail_5h_reset;
static lv_obj_t *s_claude_detail_7d_reset;
static lv_obj_t *s_claude_status;
static lv_timer_t *s_clock_timer;

static lv_color_t color(uint32_t value)
{
    return lv_color_hex(value);
}

static const char *weather_icon_for_code(int code)
{
    if (code == 0) return "☀";
    if (code <= 3) return "☁";
    if (code == 45 || code == 48) return "≋";
    if (code >= 51 && code <= 67) return "☂";
    if (code >= 71 && code <= 77) return "❄";
    if (code >= 80 && code <= 82) return "☂";
    if (code >= 85 && code <= 86) return "❄";
    if (code >= 95) return "⚡";
    return "☁";
}

static uint32_t weather_icon_color(int code)
{
    if (code == 0 || code >= 95) return COLOR_YELLOW;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return COLOR_BLUE;
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return COLOR_TEXT;
    return COLOR_MUTED;
}

static void reset_screen_refs(void)
{
    s_clock_label = NULL;
    s_date_label = NULL;
    s_location_label = NULL;
    s_network_label = NULL;
    s_weather_temp = NULL;
    s_weather_icon = NULL;
    s_weather_description = NULL;
    s_weather_detail = NULL;
    s_btc_price = NULL;
    s_btc_change = NULL;
    s_btc_chart = NULL;
    s_updated_label = NULL;
    s_wifi_status = NULL;
    s_password_textarea = NULL;
    s_location_textarea = NULL;
    for (size_t i = 0; i < APP_WEATHER_DAYS; ++i) {
        s_forecast_icon[i] = NULL;
        s_forecast_description[i] = NULL;
        s_forecast_temperature[i] = NULL;
        s_forecast_rain[i] = NULL;
    }
    s_forecast_updated = NULL;
    s_claude_5h_bar = NULL;
    s_claude_7d_bar = NULL;
    s_claude_5h_value = NULL;
    s_claude_7d_value = NULL;
    s_claude_5h_reset_compact = NULL;
    s_claude_detail_5h_bar = NULL;
    s_claude_detail_7d_bar = NULL;
    s_claude_detail_5h_value = NULL;
    s_claude_detail_7d_value = NULL;
    s_claude_detail_5h_reset = NULL;
    s_claude_detail_7d_reset = NULL;
    s_claude_status = NULL;
    s_on_claude_screen = false;
}

static lv_obj_t *prepare_screen(void)
{
    reset_screen_refs();
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, color(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(screen, &app_font_16, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    return screen;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t text_color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, color(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, color(0x22324A), 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int width, int height)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, color(COLOR_BLUE), 0);
    lv_obj_set_style_bg_color(button, color(0x0284C7), LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = make_label(button, text, &app_font_16, 0xFFFFFF);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *make_icon_button(lv_obj_t *parent, const char *symbol, int width, int height)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, color(COLOR_BLUE), 0);
    lv_obj_set_style_bg_color(button, color(0x0284C7), LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = make_label(button, symbol, &lv_font_montserrat_24, 0xFFFFFF);
    lv_obj_center(label);
    return button;
}

static const char *const CZECH_KEYBOARD_MAP[] = {
    "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "y", "x", "c", "v", "b", "n", "m", ".", "-", "\n",
    "á", "č", "ď", "é", "ě", "í", "ň", "ó", "ř", "š", "ť", "ú", "ů", "ý", "ž", "\n",
    LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

#define KEY_1 LV_BUTTONMATRIX_CTRL_WIDTH_1
#define KEY_2 LV_BUTTONMATRIX_CTRL_WIDTH_2
#define KEY_8 LV_BUTTONMATRIX_CTRL_WIDTH_8
static const lv_buttonmatrix_ctrl_t CZECH_KEYBOARD_CTRL[] = {
    KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_2,
    KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1,
    KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1,
    KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1, KEY_1,
    KEY_2, KEY_2, KEY_8, KEY_2, KEY_2
};
#undef KEY_1
#undef KEY_2
#undef KEY_8

static void configure_compact_keyboard(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    /* Keep the last row comfortably inside the 480 px round display. */
    lv_obj_set_pos(keyboard, 8, 134);
    lv_obj_set_size(keyboard, 464, 292);
    lv_obj_set_style_pad_all(keyboard, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, 3, LV_PART_MAIN);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard, textarea);
}

static void configure_czech_keyboard(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    configure_compact_keyboard(keyboard, textarea);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1,
                        CZECH_KEYBOARD_MAP, CZECH_KEYBOARD_CTRL);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
    lv_obj_set_style_text_font(keyboard, &app_font_14, LV_PART_ITEMS);
}

static lv_obj_t *make_usage_bar(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, height);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, color(0x26364D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color(COLOR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, height / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, height / 2, LV_PART_INDICATOR);
    return bar;
}

static uint32_t usage_color(int remaining)
{
    return remaining > 50 ? COLOR_GREEN : remaining > 20 ? COLOR_YELLOW : COLOR_RED;
}

static void set_usage_bar(lv_obj_t *bar, int remaining)
{
    if (bar == NULL || !lv_obj_is_valid(bar)) return;
    if (remaining < 0) remaining = 0;
    if (remaining > 100) remaining = 100;
    lv_bar_set_value(bar, remaining, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, color(usage_color(remaining)), LV_PART_INDICATOR);
}

static void format_reset(char *text, size_t capacity, time_t reset)
{
    time_t now = time(NULL);
    if (reset <= 0) {
        snprintf(text, capacity, "Čas obnovy není k dispozici");
        return;
    }
    long seconds = (long)difftime(reset, now);
    if (seconds <= 0) {
        snprintf(text, capacity, "Obnova právě probíhá");
    } else if (seconds < 3600) {
        snprintf(text, capacity, "Obnova za %ld min", (seconds + 59) / 60);
    } else if (seconds < 86400) {
        snprintf(text, capacity, "Obnova za %ld h %ld min",
                 seconds / 3600, (seconds % 3600) / 60);
    } else {
        snprintf(text, capacity, "Obnova za %ld dní %ld h",
                 seconds / 86400, (seconds % 86400) / 3600);
    }
}

static void format_compact_5h_reset(char *text, size_t capacity, time_t reset)
{
    if (reset <= 0) {
        snprintf(text, capacity, LV_SYMBOL_REFRESH " --");
        return;
    }

    long seconds = (long)difftime(reset, time(NULL));
    if (seconds <= 0) {
        snprintf(text, capacity, LV_SYMBOL_REFRESH " nyní");
    } else if (seconds < 3600) {
        snprintf(text, capacity, LV_SYMBOL_REFRESH " %ld min", (seconds + 59) / 60);
    } else {
        snprintf(text, capacity, LV_SYMBOL_REFRESH " %ld h %ld min",
                 seconds / 3600, (seconds % 3600) / 60);
    }
}

static void render_claude_locked(void)
{
    bool configured = s_config.has_claude_token;
    bool valid = configured && s_cached_claude.valid;
    int five_remaining = valid ? (int)lroundf(100.0f - s_cached_claude.five_hour_used_percent) : 0;
    int seven_remaining = valid ? (int)lroundf(100.0f - s_cached_claude.seven_day_used_percent) : 0;
    char text[64];

    if (s_claude_5h_reset_compact != NULL &&
        lv_obj_is_valid(s_claude_5h_reset_compact)) {
        format_compact_5h_reset(text, sizeof(text),
                                valid ? s_cached_claude.five_hour_reset : 0);
        lv_label_set_text(s_claude_5h_reset_compact, text);
    }

    if (s_claude_5h_value != NULL && lv_obj_is_valid(s_claude_5h_value)) {
        snprintf(text, sizeof(text), valid ? "%d %%" : configured ? "..." : "--", five_remaining);
        lv_label_set_text(s_claude_5h_value, text);
        snprintf(text, sizeof(text), valid ? "%d %%" : configured ? "..." : "--", seven_remaining);
        lv_label_set_text(s_claude_7d_value, text);
        set_usage_bar(s_claude_5h_bar, valid ? five_remaining : 0);
        set_usage_bar(s_claude_7d_bar, valid ? seven_remaining : 0);
    }

    if (s_claude_detail_5h_value != NULL && lv_obj_is_valid(s_claude_detail_5h_value)) {
        snprintf(text, sizeof(text), "%d %% zbývá", five_remaining);
        lv_label_set_text(s_claude_detail_5h_value, text);
        snprintf(text, sizeof(text), "%d %% zbývá", seven_remaining);
        lv_label_set_text(s_claude_detail_7d_value, text);
        set_usage_bar(s_claude_detail_5h_bar, five_remaining);
        set_usage_bar(s_claude_detail_7d_bar, seven_remaining);
        format_reset(text, sizeof(text), s_cached_claude.five_hour_reset);
        lv_label_set_text(s_claude_detail_5h_reset, text);
        format_reset(text, sizeof(text), s_cached_claude.seven_day_reset);
        lv_label_set_text(s_claude_detail_7d_reset, text);
    }
}

static void update_clock(lv_timer_t *timer)
{
    (void)timer;
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char clock_text[16];
    char date_text[32];
    static const char *days[] = {"Ne", "Po", "Út", "St", "Čt", "Pá", "So"};
    snprintf(clock_text, sizeof(clock_text), "%02d:%02d", local.tm_hour, local.tm_min);
    snprintf(date_text, sizeof(date_text), "%s %d.%d.", days[local.tm_wday], local.tm_mday, local.tm_mon + 1);
    if (s_clock_label != NULL && lv_obj_is_valid(s_clock_label)) lv_label_set_text(s_clock_label, clock_text);
    if (s_date_label != NULL && lv_obj_is_valid(s_date_label)) lv_label_set_text(s_date_label, date_text);
    render_claude_locked();
}

static void dashboard_event(lv_event_t *event);
static void show_settings_event(lv_event_t *event);
static void show_wifi_event(lv_event_t *event);
static void show_location_event(lv_event_t *event);
static void show_timezone_event(lv_event_t *event);
static void show_currency_event(lv_event_t *event);
static void dashboard_refresh_event(lv_event_t *event);
static void show_forecast_event(lv_event_t *event);
static void show_claude_event(lv_event_t *event);

static void render_forecast_locked(const app_weather_t *weather)
{
    if (weather == NULL || !weather->valid || weather->daily_count == 0) return;

    for (size_t i = 0; i < APP_WEATHER_DAYS; ++i) {
        if (s_forecast_icon[i] == NULL || !lv_obj_is_valid(s_forecast_icon[i])) continue;
        if (i >= weather->daily_count) {
            lv_label_set_text(s_forecast_icon[i], "—");
            lv_label_set_text(s_forecast_description[i], "Bez dat");
            lv_label_set_text(s_forecast_temperature[i], "--° / --°");
            lv_label_set_text(s_forecast_rain[i], "Déšť -- %");
            continue;
        }

        lv_label_set_text(s_forecast_icon[i], weather_icon_for_code(weather->daily_code[i]));
        lv_obj_set_style_text_color(s_forecast_icon[i],
                                    color(weather_icon_color(weather->daily_code[i])), 0);
        lv_label_set_text(s_forecast_description[i],
                          app_weather_description(weather->daily_code[i]));
        char text[40];
        snprintf(text, sizeof(text), "%.0f° / %.0f°",
                 weather->daily_max[i], weather->daily_min[i]);
        lv_label_set_text(s_forecast_temperature[i], text);
        snprintf(text, sizeof(text), "Déšť %d %%", weather->daily_precipitation[i]);
        lv_label_set_text(s_forecast_rain[i], text);
    }

    if (s_forecast_updated != NULL && lv_obj_is_valid(s_forecast_updated)) {
        struct tm local;
        localtime_r(&weather->updated_at, &local);
        char text[48];
        snprintf(text, sizeof(text), "Aktualizováno %02d:%02d", local.tm_hour, local.tm_min);
        lv_label_set_text(s_forecast_updated, text);
        lv_obj_set_style_text_color(s_forecast_updated, color(COLOR_MUTED), 0);
    }
}

static void forecast_refresh_event(lv_event_t *event)
{
    (void)event;
    if (s_forecast_updated != NULL && lv_obj_is_valid(s_forecast_updated)) {
        lv_label_set_text(s_forecast_updated, "Aktualizuji předpověď...");
        lv_obj_set_style_text_color(s_forecast_updated, color(COLOR_BLUE), 0);
    }
    if (s_callbacks.request_refresh) s_callbacks.request_refresh(s_callbacks.context);
}

static void app_ui_show_forecast(void)
{
    static const char *const day_names[] = {
        "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota"
    };

    bsp_display_lock(0);
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *back = make_button(screen, "Zpět", 82, 40);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_obj_add_event_cb(back, dashboard_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title = make_label(screen, "Předpověď", &app_font_24, COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_t *refresh = make_icon_button(screen, LV_SYMBOL_REFRESH, 48, 40);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_obj_add_event_cb(refresh, forecast_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *city = make_label(screen, s_config.city, &app_font_16, COLOR_BLUE);
    lv_obj_align(city, LV_ALIGN_TOP_MID, 0, 58);

    time_t now = time(NULL);
    struct tm day;
    localtime_r(&now, &day);
    day.tm_hour = 12;
    for (size_t i = 0; i < APP_WEATHER_DAYS; ++i) {
        if (i > 0) {
            day.tm_mday++;
            mktime(&day);
        }
        const char *day_text = i == 0 ? "Dnes" : i == 1 ? "Zítra" : day_names[day.tm_wday];
        lv_obj_t *row = make_card(screen, 42, 88 + (int)i * 62, 396, 56);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_t *day_label = make_label(row, day_text, &app_font_14,
                                         i == 0 ? COLOR_BLUE : COLOR_TEXT);
        lv_obj_set_pos(day_label, 2, 12);

        s_forecast_icon[i] = make_label(row, "☁", &app_weather_icons_32, COLOR_MUTED);
        lv_obj_set_pos(s_forecast_icon[i], 74, 5);
        s_forecast_description[i] = make_label(row, "Čekám na data", &app_font_14, COLOR_TEXT);
        lv_obj_set_pos(s_forecast_description[i], 118, 3);
        lv_obj_set_width(s_forecast_description[i], 150);
        lv_label_set_long_mode(s_forecast_description[i], LV_LABEL_LONG_DOT);
        s_forecast_rain[i] = make_label(row, "Déšť -- %", &app_font_12, COLOR_MUTED);
        lv_obj_set_pos(s_forecast_rain[i], 118, 25);
        s_forecast_temperature[i] = make_label(row, "--° / --°", &app_font_16, COLOR_TEXT);
        lv_obj_align(s_forecast_temperature[i], LV_ALIGN_RIGHT_MID, -3, 0);
    }

    s_forecast_updated = make_label(screen, "Čekám na předpověď", &app_font_12, COLOR_MUTED);
    lv_obj_align(s_forecast_updated, LV_ALIGN_TOP_MID, 0, 407);
    render_forecast_locked(&s_cached_weather);
    bsp_display_unlock();
}

static void show_forecast_event(lv_event_t *event)
{
    (void)event;
    if (s_callbacks.request_refresh) s_callbacks.request_refresh(s_callbacks.context);
    app_ui_show_forecast();
}

static void claude_refresh_event(lv_event_t *event)
{
    (void)event;
    if (s_claude_status != NULL && lv_obj_is_valid(s_claude_status)) {
        lv_label_set_text(s_claude_status, "Aktualizuji všechna data...");
        lv_obj_set_style_text_color(s_claude_status, color(COLOR_BLUE), 0);
    }
    if (s_callbacks.request_refresh != NULL) {
        s_callbacks.request_refresh(s_callbacks.context);
    }
}

static void claude_setup_event(lv_event_t *event)
{
    (void)event;
    if (s_device_address[0] == '\0') {
        if (s_claude_status != NULL && lv_obj_is_valid(s_claude_status)) {
            lv_label_set_text(s_claude_status, "Nejdřív připojte Wi-Fi");
            lv_obj_set_style_text_color(s_claude_status, color(COLOR_RED), 0);
        }
        return;
    }
    if (s_callbacks.enable_claude_setup != NULL) {
        s_callbacks.enable_claude_setup(s_callbacks.context);
    }
    if (s_claude_status != NULL && lv_obj_is_valid(s_claude_status)) {
        lv_label_set_text(s_claude_status, "Nastavení povoleno na 10 minut");
        lv_obj_set_style_text_color(s_claude_status, color(COLOR_GREEN), 0);
    }
}

static lv_obj_t *make_usage_detail_card(lv_obj_t *screen, int y, const char *title,
                                        lv_obj_t **value, lv_obj_t **bar,
                                        lv_obj_t **reset)
{
    lv_obj_t *card = make_card(screen, 12, y, 456, 138);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_t *title_label = make_label(card, title, &app_font_16, COLOR_MUTED);
    lv_obj_set_pos(title_label, 2, 0);
    *value = make_label(card, "-- % zbývá", &app_font_28, COLOR_TEXT);
    lv_obj_set_pos(*value, 2, 26);
    *bar = make_usage_bar(card, 2, 72, 422, 14);
    *reset = make_label(card, "Čekám na data", &app_font_14, COLOR_MUTED);
    lv_obj_set_pos(*reset, 2, 94);
    return card;
}

static void app_ui_show_claude(void)
{
    bsp_display_lock(0);
    lv_obj_t *screen = prepare_screen();
    s_on_claude_screen = true;
    lv_obj_t *back = make_button(screen, "Zpět", 82, 40);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_obj_add_event_cb(back, dashboard_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title = make_label(screen, "Claude limity", &app_font_24, COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    if (!s_config.has_claude_token) {
        lv_obj_t *card = make_card(screen, 12, 72, 456, 252);
        lv_obj_t *state = make_label(card, "Token není nastavený", &app_font_20, COLOR_YELLOW);
        lv_obj_set_pos(state, 0, 0);
        lv_obj_t *instructions = make_label(card,
            "1. Na počítači spusťte:\n   claude setup-token\n\n"
            "2. Níže povolte nastavení.\n\n"
            "3. V prohlížeči otevřete:",
            &app_font_16, COLOR_TEXT);
        lv_obj_set_pos(instructions, 0, 38);
        lv_obj_set_width(instructions, 420);
        lv_label_set_long_mode(instructions, LV_LABEL_LONG_WRAP);
        const char *address = s_device_address[0] ? s_device_address : "Wi-Fi zatím nemá IP adresu";
        lv_obj_t *url = make_label(card, address, &app_font_18,
                                   s_device_address[0] ? COLOR_BLUE : COLOR_MUTED);
        lv_obj_set_pos(url, 0, 194);
        lv_obj_set_width(url, 420);
        lv_label_set_long_mode(url, LV_LABEL_LONG_DOT);

        lv_obj_t *enable = make_button(screen, "Povolit nastavení na 10 minut", 330, 52);
        lv_obj_align(enable, LV_ALIGN_TOP_MID, 0, 340);
        lv_obj_add_event_cb(enable, claude_setup_event, LV_EVENT_CLICKED, NULL);
        s_claude_status = make_label(screen, "Token se na displeji nezobrazuje", &app_font_14, COLOR_MUTED);
        lv_obj_align(s_claude_status, LV_ALIGN_TOP_MID, 0, 410);
    } else {
        lv_obj_t *refresh = make_icon_button(screen, LV_SYMBOL_REFRESH, 48, 40);
        lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -12, 10);
        lv_obj_add_event_cb(refresh, claude_refresh_event, LV_EVENT_CLICKED, NULL);
        make_usage_detail_card(screen, 70, "5hodinové okno",
                               &s_claude_detail_5h_value, &s_claude_detail_5h_bar,
                               &s_claude_detail_5h_reset);
        make_usage_detail_card(screen, 218, "Týdenní okno",
                               &s_claude_detail_7d_value, &s_claude_detail_7d_bar,
                               &s_claude_detail_7d_reset);
        lv_obj_t *change = make_button(screen, "Změnit token", 154, 42);
        lv_obj_set_pos(change, 12, 374);
        lv_obj_set_style_bg_color(change, color(COLOR_CARD_ALT), 0);
        lv_obj_add_event_cb(change, claude_setup_event, LV_EVENT_CLICKED, NULL);
        const char *address = s_device_address[0] ? s_device_address : "čekám na IP adresu";
        lv_obj_t *url = make_label(screen, address, &app_font_12, COLOR_MUTED);
        lv_obj_set_pos(url, 178, 387);
        lv_obj_set_width(url, 290);
        lv_label_set_long_mode(url, LV_LABEL_LONG_DOT);
        s_claude_status = make_label(screen, "Čekám na limity", &app_font_12, COLOR_MUTED);
        lv_obj_align(s_claude_status, LV_ALIGN_TOP_MID, 0, 432);
        render_claude_locked();
        if (s_cached_claude.valid) {
            struct tm local;
            localtime_r(&s_cached_claude.updated_at, &local);
            char updated[48];
            snprintf(updated, sizeof(updated), "Aktualizováno %02d:%02d", local.tm_hour, local.tm_min);
            lv_label_set_text(s_claude_status, updated);
        }
    }
    bsp_display_unlock();
}

static void show_claude_event(lv_event_t *event)
{
    (void)event;
    if (s_callbacks.request_refresh) s_callbacks.request_refresh(s_callbacks.context);
    app_ui_show_claude();
}

static int32_t candle_y(double value, double minimum, double maximum,
                        int32_t top, int32_t bottom)
{
    double range = maximum - minimum;
    if (range <= 0.0) return (top + bottom) / 2;
    double position = (value - minimum) / range;
    return bottom - (int32_t)llround(position * (bottom - top));
}

static void btc_candles_draw_event(lv_event_t *event)
{
    if (!s_cached_btc.valid || s_cached_btc.point_count == 0) return;

    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    int32_t left = area.x1 + 2;
    int32_t right = area.x2 - 2;
    int32_t top = area.y1 + 3;
    int32_t bottom = area.y2 - 3;
    int32_t width = right - left + 1;

    double minimum = s_cached_btc.candles[0].low;
    double maximum = s_cached_btc.candles[0].high;
    for (size_t i = 1; i < s_cached_btc.point_count; ++i) {
        if (s_cached_btc.candles[i].low < minimum) minimum = s_cached_btc.candles[i].low;
        if (s_cached_btc.candles[i].high > maximum) maximum = s_cached_btc.candles[i].high;
    }
    double padding = fmax((maximum - minimum) * 0.06, 20.0);
    minimum -= padding;
    maximum += padding;

    lv_draw_line_dsc_t grid;
    lv_draw_line_dsc_init(&grid);
    grid.color = color(0x26364D);
    grid.opa = LV_OPA_60;
    grid.width = 1;
    for (int row = 1; row <= 3; ++row) {
        grid.p1.x = left;
        grid.p2.x = right;
        grid.p1.y = grid.p2.y = top + (bottom - top) * row / 4;
        lv_draw_line(layer, &grid);
    }
    for (int column = 1; column <= 3; ++column) {
        grid.p1.y = top;
        grid.p2.y = bottom;
        grid.p1.x = grid.p2.x = left + (right - left) * column / 4;
        lv_draw_line(layer, &grid);
    }

    int32_t slot = width / (int32_t)s_cached_btc.point_count;
    int32_t body_width = slot * 3 / 5;
    if (body_width < 5) body_width = 5;
    if (body_width > 11) body_width = 11;
    if ((body_width & 1) == 0) body_width--;
    int32_t half = body_width / 2;

    for (size_t i = 0; i < s_cached_btc.point_count; ++i) {
        const app_btc_candle_t *candle = &s_cached_btc.candles[i];
        bool rising = candle->close >= candle->open;
        lv_color_t candle_color = color(rising ? 0x4ADE80 : 0xF43F5E);
        int32_t x = left + ((int32_t)i * width + width / 2) /
                              (int32_t)s_cached_btc.point_count;

        lv_draw_line_dsc_t wick;
        lv_draw_line_dsc_init(&wick);
        wick.color = candle_color;
        wick.width = 2;
        wick.p1.x = wick.p2.x = x;
        wick.p1.y = candle_y(candle->high, minimum, maximum, top, bottom);
        wick.p2.y = candle_y(candle->low, minimum, maximum, top, bottom);
        lv_draw_line(layer, &wick);

        int32_t open_y = candle_y(candle->open, minimum, maximum, top, bottom);
        int32_t close_y = candle_y(candle->close, minimum, maximum, top, bottom);
        lv_area_t body = {
            .x1 = x - half,
            .x2 = x + half,
            .y1 = LV_MIN(open_y, close_y),
            .y2 = LV_MAX(open_y, close_y),
        };
        if (body.y2 == body.y1) body.y2++;
        lv_draw_rect_dsc_t body_style;
        lv_draw_rect_dsc_init(&body_style);
        body_style.bg_color = candle_color;
        body_style.bg_opa = LV_OPA_COVER;
        body_style.border_color = color(rising ? 0x16A34A : 0xBE123C);
        body_style.border_width = 1;
        body_style.radius = 0;
        lv_draw_rect(layer, &body_style, &body);
    }
}

void app_ui_show_dashboard(const app_config_t *config)
{
    if (config != NULL) s_config = *config;
    bsp_display_lock(0);
    lv_obj_t *screen = prepare_screen();
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, dashboard_refresh_event, LV_EVENT_CLICKED, NULL);

    s_clock_label = make_label(screen, "--:--", &lv_font_montserrat_40, COLOR_TEXT);
    lv_obj_set_pos(s_clock_label, 16, 10);
    s_date_label = make_label(screen, "-- --.--.", &app_font_14, COLOR_MUTED);
    lv_obj_set_pos(s_date_label, 22, 54);

    lv_obj_t *claude = make_card(screen, 145, 7, 247, 64);
    lv_obj_set_style_pad_all(claude, 5, 0);
    lv_obj_set_style_radius(claude, 14, 0);
    lv_obj_add_flag(claude, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(claude, show_claude_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *claude_title = make_label(claude, "CLAUDE", &app_font_12, COLOR_YELLOW);
    lv_obj_set_pos(claude_title, 2, -4);
    s_claude_5h_reset_compact = make_label(claude, LV_SYMBOL_REFRESH " --", &app_font_12, COLOR_MUTED);
    lv_obj_set_pos(s_claude_5h_reset_compact, 58, -4);
    lv_obj_set_width(s_claude_5h_reset_compact, 176);
    lv_obj_set_style_text_align(s_claude_5h_reset_compact, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_claude_5h_reset_compact, LV_LABEL_LONG_CLIP);
    lv_obj_t *five_label = make_label(claude, "5 h", &app_font_12, COLOR_MUTED);
    lv_obj_set_pos(five_label, 2, 15);
    lv_obj_t *seven_label = make_label(claude, "7 d", &app_font_12, COLOR_MUTED);
    lv_obj_set_pos(seven_label, 2, 36);
    s_claude_5h_bar = make_usage_bar(claude, 31, 21, 140, 7);
    s_claude_7d_bar = make_usage_bar(claude, 31, 42, 140, 7);
    s_claude_5h_value = make_label(claude, "--", &app_font_12, COLOR_TEXT);
    lv_obj_set_pos(s_claude_5h_value, 180, 13);
    s_claude_7d_value = make_label(claude, "--", &app_font_12, COLOR_TEXT);
    lv_obj_set_pos(s_claude_7d_value, 180, 34);

    s_network_label = lv_obj_create(screen);
    lv_obj_remove_style_all(s_network_label);
    lv_obj_set_pos(s_network_label, 400, 29);
    lv_obj_set_size(s_network_label, 9, 9);
    lv_obj_set_style_radius(s_network_label, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_network_label, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_network_label, color(COLOR_MUTED), 0);
    lv_obj_t *settings = make_icon_button(screen, LV_SYMBOL_SETTINGS, 52, 42);
    lv_obj_align(settings, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_set_style_bg_opa(settings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(settings, color(COLOR_CARD_ALT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(settings, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(settings, 0, 0);
    lv_obj_set_style_shadow_width(settings, 0, 0);
    lv_obj_t *settings_icon = lv_obj_get_child(settings, 0);
    if (settings_icon != NULL) {
        lv_obj_set_style_text_color(settings_icon, color(COLOR_MUTED), 0);
    }
    lv_obj_add_event_cb(settings, show_settings_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *weather = make_card(screen, 12, 78, 456, 130);
    lv_obj_add_flag(weather, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(weather, dashboard_refresh_event, LV_EVENT_CLICKED, NULL);
    s_location_label = make_label(weather, s_config.city, &app_font_16, COLOR_BLUE);
    lv_obj_set_pos(s_location_label, 2, 0);
    lv_obj_set_width(s_location_label, 260);
    lv_label_set_long_mode(s_location_label, LV_LABEL_LONG_DOT);
    lv_obj_t *forecast = make_button(weather, "Výhled na 5 dní", 146, 32);
    lv_obj_align(forecast, LV_ALIGN_TOP_RIGHT, -2, -4);
    lv_obj_set_style_bg_color(forecast, color(COLOR_CARD_ALT), 0);
    lv_obj_set_style_bg_color(forecast, color(0x22324A), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(forecast, 0, 0);
    lv_obj_set_style_radius(forecast, 16, 0);
    lv_obj_t *forecast_label = lv_obj_get_child(forecast, 0);
    if (forecast_label != NULL) {
        lv_obj_set_style_text_color(forecast_label, color(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(forecast_label, &app_font_14, 0);
    }
    lv_obj_remove_flag(forecast, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(forecast, show_forecast_event, LV_EVENT_CLICKED, NULL);
    s_weather_temp = make_label(weather, "-- °C", &lv_font_montserrat_48, COLOR_TEXT);
    lv_obj_set_pos(s_weather_temp, 0, 27);
    s_weather_icon = make_label(weather, "☁", &app_weather_icons_48, COLOR_MUTED);
    lv_obj_set_pos(s_weather_icon, 126, 28);
    s_weather_description = make_label(weather, "Čekám na počasí", &app_font_20, COLOR_TEXT);
    lv_obj_set_pos(s_weather_description, 188, 29);
    s_weather_detail = make_label(weather, "Pocitově --  |  Déšť --  |  -- / --", &app_font_14, COLOR_MUTED);
    lv_obj_set_pos(s_weather_detail, 188, 68);

    lv_obj_t *btc = make_card(screen, 12, 218, 456, 232);
    lv_obj_add_flag(btc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btc, dashboard_refresh_event, LV_EVENT_CLICKED, NULL);
    char btc_title_text[40];
    snprintf(btc_title_text, sizeof(btc_title_text), "BTC / %s  |  1 h",
             s_config.btc_currency);
    lv_obj_t *btc_title = make_label(btc, btc_title_text, &app_font_16, COLOR_YELLOW);
    lv_obj_set_pos(btc_title, 2, 0);
    char price_placeholder[16];
    if (strcmp(s_config.btc_currency, "USD") == 0) {
        strlcpy(price_placeholder, "$ --", sizeof(price_placeholder));
    } else if (strcmp(s_config.btc_currency, "EUR") == 0) {
        strlcpy(price_placeholder, "€ --", sizeof(price_placeholder));
    } else {
        strlcpy(price_placeholder, "-- Kč", sizeof(price_placeholder));
    }
    s_btc_price = make_label(btc, price_placeholder, &app_font_32, COLOR_TEXT);
    lv_obj_set_pos(s_btc_price, 2, 25);
    lv_obj_set_size(s_btc_price, 285, 44);
    lv_label_set_long_mode(s_btc_price, LV_LABEL_LONG_CLIP);
    s_btc_change = make_label(btc, "-- % / 24 h", &app_font_16, COLOR_MUTED);
    lv_obj_align(s_btc_change, LV_ALIGN_TOP_RIGHT, -2, 34);

    s_btc_chart = lv_obj_create(btc);
    lv_obj_remove_style_all(s_btc_chart);
    lv_obj_set_pos(s_btc_chart, 0, 72);
    lv_obj_set_size(s_btc_chart, 422, 106);
    lv_obj_set_style_bg_opa(s_btc_chart, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_btc_chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_btc_chart, btc_candles_draw_event, LV_EVENT_DRAW_MAIN, NULL);

    s_updated_label = make_label(screen,
                                 s_data_refreshing ? "Aktualizuji data..." : "Čekám na data",
                                 &app_font_12,
                                 s_data_refreshing ? COLOR_BLUE : COLOR_MUTED);
    lv_obj_set_width(s_updated_label, 456);
    lv_obj_set_style_text_align(s_updated_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_updated_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    update_clock(NULL);
    render_claude_locked();
    bsp_display_unlock();

    /* Data may have arrived while a settings screen was open.  Always redraw
     * the last successful updates when returning to the dashboard. */
    if (s_cached_weather.valid) app_ui_update_weather(&s_cached_weather);
    if (s_cached_btc.valid &&
        strcmp(s_cached_btc.currency, s_config.btc_currency) == 0) {
        app_ui_update_btc(&s_cached_btc);
    }
}

static void welcome_connect_event(lv_event_t *event)
{
    (void)event;
    if (s_callbacks.request_wifi_scan) s_callbacks.request_wifi_scan(s_callbacks.context);
}

void app_ui_show_welcome(void)
{
    bsp_display_lock(0);
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *badge = make_label(screen, "KRABIČKA", &app_font_18, COLOR_BLUE);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_t *title = make_label(screen, "Osobní dashboard", &app_font_32, COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_t *description = make_label(screen,
        "Počasí, přesný čas a Bitcoin\nna jednom místě.",
        &app_font_18, COLOR_MUTED);
    lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 164);
    lv_obj_t *button = make_button(screen, "Připojit k Wi-Fi", 300, 64);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 76);
    lv_obj_add_event_cb(button, welcome_connect_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *defaults = make_label(screen, "Výchozí: Praha  |  Europe/Prague", &app_font_14, COLOR_MUTED);
    lv_obj_align(defaults, LV_ALIGN_BOTTOM_MID, 0, -36);
    bsp_display_unlock();
}

void app_ui_show_wifi_scanning(void)
{
    bsp_display_lock(0);
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *title = make_label(screen, "Hledám Wi-Fi...", &app_font_28, COLOR_TEXT);
    lv_obj_center(title);
    lv_obj_t *spinner = lv_spinner_create(screen);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -72);
    lv_obj_set_style_arc_color(spinner, color(COLOR_BLUE), LV_PART_INDICATOR);
    bsp_display_unlock();
}

static void wifi_selected_event(lv_event_t *event)
{
    app_wifi_ap_t *ap = lv_event_get_user_data(event);
    if (ap == NULL) return;
    strlcpy(s_selected_ssid, ap->ssid, sizeof(s_selected_ssid));
    if (!ap->secured) {
        if (s_callbacks.connect_wifi) s_callbacks.connect_wifi(s_selected_ssid, "", s_callbacks.context);
        return;
    }

    lv_obj_t *screen = prepare_screen();
    char title_text[80];
    snprintf(title_text, sizeof(title_text), "Heslo pro %s", s_selected_ssid);
    lv_obj_t *title = make_label(screen, title_text, &app_font_20, COLOR_TEXT);
    lv_obj_set_pos(title, 16, 10);
    s_wifi_status = make_label(screen, "Potvrďte klávesou OK", &app_font_14, COLOR_MUTED);
    lv_obj_set_pos(s_wifi_status, 18, 38);
    s_password_textarea = lv_textarea_create(screen);
    lv_obj_set_pos(s_password_textarea, 16, 62);
    lv_obj_set_size(s_password_textarea, 448, 52);
    lv_textarea_set_one_line(s_password_textarea, true);
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_textarea_set_placeholder_text(s_password_textarea, "Heslo k Wi-Fi");
    lv_obj_t *keyboard = lv_keyboard_create(screen);
    configure_compact_keyboard(keyboard, s_password_textarea);
    lv_obj_add_event_cb(keyboard, dashboard_event, LV_EVENT_READY, (void *)1);
    lv_obj_add_event_cb(keyboard, show_wifi_event, LV_EVENT_CANCEL, NULL);
}

static void dashboard_event(lv_event_t *event)
{
    if (lv_event_get_user_data(event) == (void *)1 && s_password_textarea != NULL) {
        const char *password = lv_textarea_get_text(s_password_textarea);
        if (s_callbacks.connect_wifi) s_callbacks.connect_wifi(s_selected_ssid, password, s_callbacks.context);
        return;
    }
    app_ui_show_dashboard(&s_config);
}

static void show_wifi_event(lv_event_t *event)
{
    (void)event;
    if (s_callbacks.request_wifi_scan) s_callbacks.request_wifi_scan(s_callbacks.context);
}

void app_ui_show_wifi_results(const app_wifi_ap_t *results, size_t count, const char *error)
{
    if (count > APP_WIFI_AP_MAX) count = APP_WIFI_AP_MAX;
    if (results != NULL && count > 0) memcpy(s_aps, results, count * sizeof(results[0]));
    s_ap_count = count;

    bsp_display_lock(0);
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *title = make_label(screen, "Vyberte Wi-Fi", &app_font_28, COLOR_TEXT);
    lv_obj_set_pos(title, 16, 14);
    lv_obj_t *refresh = make_button(screen, "Obnovit", 112, 44);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -14, 10);
    lv_obj_add_event_cb(refresh, show_wifi_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_pos(list, 12, 70);
    lv_obj_set_size(list, 456, 398);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);

    if (error != NULL || count == 0) {
        lv_obj_t *message = make_label(list, error ? error : "Žádné sítě nebyly nalezeny", &app_font_16, COLOR_MUTED);
        lv_obj_set_width(message, 440);
    }
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *button = lv_button_create(list);
        lv_obj_set_size(button, 440, 62);
        lv_obj_set_style_bg_color(button, color(COLOR_CARD), 0);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, wifi_selected_event, LV_EVENT_CLICKED, &s_aps[i]);
        lv_obj_t *name = make_label(button, s_aps[i].ssid, &app_font_16, COLOR_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, 0);
        char info[32];
        snprintf(info, sizeof(info), "%s  %d dBm", s_aps[i].secured ? "Heslo" : "Volná", s_aps[i].rssi);
        lv_obj_t *signal = make_label(button, info, &app_font_12, COLOR_MUTED);
        lv_obj_align(signal, LV_ALIGN_RIGHT_MID, -4, 0);
    }
    bsp_display_unlock();
}

void app_ui_set_network_state(app_network_state_t state, const char *detail)
{
    bsp_display_lock(0);
    if (s_network_label != NULL && lv_obj_is_valid(s_network_label)) {
        lv_obj_set_style_bg_color(s_network_label,
            color(state == APP_NETWORK_CONNECTED ? COLOR_GREEN :
                  state == APP_NETWORK_FAILED ? COLOR_RED : COLOR_MUTED), 0);
    }
    if (s_wifi_status != NULL && lv_obj_is_valid(s_wifi_status)) {
        lv_label_set_text(s_wifi_status, detail ? detail : "");
        lv_obj_set_style_text_color(s_wifi_status,
            color(state == APP_NETWORK_FAILED ? COLOR_RED : COLOR_MUTED), 0);
    }
    if (s_updated_label != NULL && lv_obj_is_valid(s_updated_label)) {
        if (state == APP_NETWORK_CONNECTED) {
            lv_label_set_text(s_updated_label, "Načítám BTC...");
            lv_obj_set_style_text_color(s_updated_label, color(COLOR_MUTED), 0);
        } else if (state == APP_NETWORK_FAILED || state == APP_NETWORK_DISCONNECTED) {
            lv_label_set_text(s_updated_label, "BTC: Wi-Fi není připojena");
            lv_obj_set_style_text_color(s_updated_label, color(COLOR_RED), 0);
        } else if (state == APP_NETWORK_CONNECTING) {
            lv_label_set_text(s_updated_label, "BTC čeká na Wi-Fi...");
            lv_obj_set_style_text_color(s_updated_label, color(COLOR_MUTED), 0);
        }
    }
    bsp_display_unlock();
}

void app_ui_set_internet_available(bool available)
{
    bsp_display_lock(0);
    if (s_network_label != NULL && lv_obj_is_valid(s_network_label)) {
        lv_obj_set_style_bg_color(s_network_label,
                                  color(available ? COLOR_GREEN : COLOR_RED), 0);
    }
    bsp_display_unlock();
}

void app_ui_set_data_refreshing(bool refreshing)
{
    bsp_display_lock(0);
    s_data_refreshing = refreshing;
    if (s_updated_label != NULL && lv_obj_is_valid(s_updated_label)) {
        if (refreshing) {
            lv_label_set_text(s_updated_label, "Aktualizuji data...");
            lv_obj_set_style_text_color(s_updated_label, color(COLOR_BLUE), 0);
        } else if (strncmp(lv_label_get_text(s_updated_label), "Aktualizuji", 11) == 0) {
            if (s_cached_btc.valid) {
                struct tm local;
                localtime_r(&s_cached_btc.updated_at, &local);
                char text[48];
                snprintf(text, sizeof(text), "Aktualizováno %02d:%02d",
                         local.tm_hour, local.tm_min);
                lv_label_set_text(s_updated_label, text);
            } else {
                lv_label_set_text(s_updated_label, "Čekám na data");
            }
            lv_obj_set_style_text_color(s_updated_label, color(COLOR_MUTED), 0);
        }
    }
    bsp_display_unlock();
}

void app_ui_set_btc_status(const char *message, bool error)
{
    bsp_display_lock(0);
    if (s_updated_label != NULL && lv_obj_is_valid(s_updated_label)) {
        lv_label_set_text(s_updated_label, message ? message : "");
        lv_obj_set_style_text_color(s_updated_label, color(error ? COLOR_RED : COLOR_MUTED), 0);
    }
    bsp_display_unlock();
}

void app_ui_update_weather(const app_weather_t *weather)
{
    if (weather == NULL || !weather->valid) return;
    if (weather != &s_cached_weather) s_cached_weather = *weather;
    bsp_display_lock(0);
    if (s_weather_temp != NULL && lv_obj_is_valid(s_weather_temp)) {
        char text[96];
        snprintf(text, sizeof(text), "%.0f °C", weather->temperature);
        lv_label_set_text(s_weather_temp, text);
        if (s_weather_icon != NULL && lv_obj_is_valid(s_weather_icon)) {
            lv_label_set_text(s_weather_icon, weather_icon_for_code(weather->weather_code));
            lv_obj_set_style_text_color(s_weather_icon,
                                        color(weather_icon_color(weather->weather_code)), 0);
        }
        lv_label_set_text(s_weather_description, app_weather_description(weather->weather_code));
        snprintf(text, sizeof(text), "Pocitově %.0f°  |  Déšť %d%%  |  %.0f / %.0f",
                 weather->apparent_temperature, weather->precipitation_probability,
                 weather->temperature_max, weather->temperature_min);
        lv_label_set_text(s_weather_detail, text);
    }
    render_forecast_locked(weather);
    bsp_display_unlock();
}

void app_ui_clear_weather_cache(void)
{
    bsp_display_lock(0);
    memset(&s_cached_weather, 0, sizeof(s_cached_weather));
    bsp_display_unlock();
}

void app_ui_update_btc(const app_btc_t *btc)
{
    if (btc == NULL || !btc->valid) return;
    if (strcmp(btc->currency, s_config.btc_currency) != 0) return;
    if (btc != &s_cached_btc) s_cached_btc = *btc;
    bsp_display_lock(0);
    bool price_visible = s_btc_price != NULL && lv_obj_is_valid(s_btc_price);
    ESP_LOGI(TAG, "BTC redraw: price=%.2f %s points=%u label=%s",
             btc->price, btc->currency, (unsigned)btc->point_count,
             price_visible ? "ready" : "not on dashboard");
    if (price_visible) {
        char text[48];
        char digits[24];
        char grouped[32];
        snprintf(digits, sizeof(digits), "%llu",
                 (unsigned long long)llround(btc->price));
        size_t digit_count = strlen(digits);
        size_t grouped_at = 0;
        for (size_t i = 0; i < digit_count && grouped_at + 1 < sizeof(grouped); ++i) {
            grouped[grouped_at++] = digits[i];
            size_t remaining = digit_count - i - 1;
            if (remaining > 0 && remaining % 3 == 0 && grouped_at + 1 < sizeof(grouped)) {
                grouped[grouped_at++] = ' ';
            }
        }
        grouped[grouped_at] = '\0';
        if (strcmp(btc->currency, "USD") == 0) {
            snprintf(text, sizeof(text), "$ %s", grouped);
        } else if (strcmp(btc->currency, "EUR") == 0) {
            snprintf(text, sizeof(text), "€ %s", grouped);
        } else {
            snprintf(text, sizeof(text), "%s Kč", grouped);
        }
        lv_label_set_text(s_btc_price, text);
        lv_obj_invalidate(s_btc_price);
        ESP_LOGI(TAG, "BTC price label set to: %s", lv_label_get_text(s_btc_price));
        snprintf(text, sizeof(text), "%+.2f %% / 24 h", btc->change_percent_24h);
        lv_label_set_text(s_btc_change, text);
        lv_obj_set_style_text_color(s_btc_change,
            color(btc->change_percent_24h >= 0 ? COLOR_GREEN : COLOR_RED), 0);

        if (s_btc_chart != NULL && lv_obj_is_valid(s_btc_chart) && btc->point_count > 0) {
            lv_obj_invalidate(s_btc_chart);
        }
        if (!s_data_refreshing) {
            struct tm local;
            localtime_r(&btc->updated_at, &local);
            snprintf(text, sizeof(text), "Aktualizováno %02d:%02d", local.tm_hour, local.tm_min);
            lv_label_set_text(s_updated_label, text);
            lv_obj_set_style_text_color(s_updated_label, color(COLOR_MUTED), 0);
        }
    }
    bsp_display_unlock();
}

void app_ui_update_claude(const app_claude_usage_t *usage)
{
    if (usage == NULL || !usage->valid) return;
    if (usage != &s_cached_claude) s_cached_claude = *usage;
    bsp_display_lock(0);
    render_claude_locked();
    if (s_claude_status != NULL && lv_obj_is_valid(s_claude_status)) {
        struct tm local;
        localtime_r(&usage->updated_at, &local);
        char text[48];
        snprintf(text, sizeof(text), "Aktualizováno %02d:%02d", local.tm_hour, local.tm_min);
        lv_label_set_text(s_claude_status, text);
        lv_obj_set_style_text_color(s_claude_status, color(COLOR_MUTED), 0);
    }
    bsp_display_unlock();
}

void app_ui_set_claude_status(const char *message, bool error)
{
    bsp_display_lock(0);
    if (s_claude_status != NULL && lv_obj_is_valid(s_claude_status)) {
        lv_label_set_text(s_claude_status, message ? message : "");
        lv_obj_set_style_text_color(s_claude_status,
                                    color(error ? COLOR_RED : COLOR_MUTED), 0);
    }
    bsp_display_unlock();
}

void app_ui_set_claude_configured(bool configured)
{
    s_config.has_claude_token = configured;
    if (!configured) memset(&s_cached_claude, 0, sizeof(s_cached_claude));
    if (s_on_claude_screen) {
        app_ui_show_claude();
        return;
    }
    bsp_display_lock(0);
    render_claude_locked();
    bsp_display_unlock();
}

void app_ui_set_device_address(const char *address)
{
    if (address != NULL && address[0] != '\0') {
        snprintf(s_device_address, sizeof(s_device_address), "http://%s/", address);
    } else {
        s_device_address[0] = '\0';
    }
    if (s_on_claude_screen) app_ui_show_claude();
}

static void location_keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
        app_ui_show_dashboard(&s_config);
        return;
    }
    if (s_location_textarea == NULL || s_callbacks.request_geocode == NULL) return;
    const char *query = lv_textarea_get_text(s_location_textarea);
    if (query[0] == '\0') return;
    s_callbacks.request_geocode(query, s_callbacks.context);
}

static void show_location_event(lv_event_t *event)
{
    (void)event;
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *title = make_label(screen, "Místo pro počasí", &app_font_24, COLOR_TEXT);
    lv_obj_set_pos(title, 16, 14);
    lv_obj_t *hint = make_label(screen, "Napište město a stiskněte OK", &app_font_14, COLOR_MUTED);
    lv_obj_set_pos(hint, 18, 50);
    s_location_textarea = lv_textarea_create(screen);
    lv_obj_set_pos(s_location_textarea, 16, 76);
    lv_obj_set_size(s_location_textarea, 448, 58);
    lv_textarea_set_one_line(s_location_textarea, true);
    lv_textarea_set_text(s_location_textarea, s_config.city);
    lv_obj_t *keyboard = lv_keyboard_create(screen);
    configure_czech_keyboard(keyboard, s_location_textarea);
    lv_obj_add_event_cb(keyboard, location_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, location_keyboard_event, LV_EVENT_CANCEL, NULL);
}

static void geo_selected_event(lv_event_t *event)
{
    app_geo_result_t *location = lv_event_get_user_data(event);
    if (location && s_callbacks.select_location) s_callbacks.select_location(location, s_callbacks.context);
}

void app_ui_show_geocode_results(const app_geo_result_t *results, size_t count, const char *error)
{
    if (count > APP_GEO_RESULTS_MAX) count = APP_GEO_RESULTS_MAX;
    if (results && count) memcpy(s_geo, results, count * sizeof(results[0]));
    s_geo_count = count;
    bsp_display_lock(0);
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *title = make_label(screen, "Vyberte místo", &app_font_28, COLOR_TEXT);
    lv_obj_set_pos(title, 16, 16);
    lv_obj_t *back = make_button(screen, "Zpět", 92, 44);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -14, 10);
    lv_obj_add_event_cb(back, show_location_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_pos(list, 12, 72);
    lv_obj_set_size(list, 456, 396);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    if (error || count == 0) {
        make_label(list, error ? error : "Místo nebylo nalezeno", &app_font_16, COLOR_MUTED);
    }
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *button = lv_button_create(list);
        lv_obj_set_size(button, 440, 66);
        lv_obj_set_style_bg_color(button, color(COLOR_CARD), 0);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, geo_selected_event, LV_EVENT_CLICKED, &s_geo[i]);
        char line[112];
        snprintf(line, sizeof(line), "%.63s, %.39s", s_geo[i].name, s_geo[i].country);
        lv_obj_t *name = make_label(button, line, &app_font_16, COLOR_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, -10);
        lv_obj_t *zone = make_label(button, s_geo[i].timezone, &app_font_12, COLOR_MUTED);
        lv_obj_align(zone, LV_ALIGN_LEFT_MID, 4, 14);
    }
    bsp_display_unlock();
}

typedef struct {
    const char *name;
    const char *posix;
} timezone_option_t;

static const timezone_option_t TIMEZONES[] = {
    {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Asia/Tokyo", "JST-9"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};

static void timezone_selected_event(lv_event_t *event)
{
    const timezone_option_t *zone = lv_event_get_user_data(event);
    if (zone && s_callbacks.select_timezone) s_callbacks.select_timezone(zone->name, zone->posix, s_callbacks.context);
}

static void show_timezone_event(lv_event_t *event)
{
    (void)event;
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *title = make_label(screen, "Časové pásmo", &app_font_28, COLOR_TEXT);
    lv_obj_set_pos(title, 16, 16);
    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_pos(list, 12, 72);
    lv_obj_set_size(list, 456, 396);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    for (size_t i = 0; i < sizeof(TIMEZONES) / sizeof(TIMEZONES[0]); ++i) {
        lv_obj_t *button = lv_button_create(list);
        lv_obj_set_size(button, 440, 58);
        lv_obj_set_style_bg_color(button, color(COLOR_CARD), 0);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, timezone_selected_event, LV_EVENT_CLICKED, (void *)&TIMEZONES[i]);
        lv_obj_t *label = make_label(button, TIMEZONES[i].name, &app_font_16,
                                     strcmp(TIMEZONES[i].name, s_config.timezone) == 0 ? COLOR_BLUE : COLOR_TEXT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 4, 0);
    }
}

static const char *const CURRENCIES[] = {"USD", "EUR", "CZK"};

static void currency_selected_event(lv_event_t *event)
{
    const char *currency = lv_event_get_user_data(event);
    if (currency != NULL && s_callbacks.select_currency != NULL) {
        s_callbacks.select_currency(currency, s_callbacks.context);
    }
}

static void show_currency_event(lv_event_t *event)
{
    (void)event;
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *title = make_label(screen, "Měna pro Bitcoin", &app_font_28, COLOR_TEXT);
    lv_obj_set_pos(title, 16, 16);
    lv_obj_t *hint = make_label(screen, "Vyberte měnu ceny a grafu", &app_font_14, COLOR_MUTED);
    lv_obj_set_pos(hint, 18, 52);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_pos(list, 12, 88);
    lv_obj_set_size(list, 456, 300);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);

    for (size_t i = 0; i < sizeof(CURRENCIES) / sizeof(CURRENCIES[0]); ++i) {
        const char *currency = CURRENCIES[i];
        lv_obj_t *button = lv_button_create(list);
        lv_obj_set_size(button, 440, 64);
        lv_obj_set_style_bg_color(button,
            color(strcmp(currency, s_config.btc_currency) == 0 ? 0x075985 : COLOR_CARD), 0);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, currency_selected_event, LV_EVENT_CLICKED, (void *)currency);
        lv_obj_t *label = make_label(button, currency, &lv_font_montserrat_20, COLOR_TEXT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
        if (strcmp(currency, s_config.btc_currency) == 0) {
            lv_obj_t *selected = make_label(button, "Vybráno", &app_font_14, COLOR_GREEN);
            lv_obj_align(selected, LV_ALIGN_RIGHT_MID, -8, 0);
        }
    }
}

static void brightness_event(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    int value = lv_slider_get_value(slider);
    if (s_callbacks.set_brightness) s_callbacks.set_brightness((uint8_t)value, s_callbacks.context);
}

static void forget_wifi_event(lv_event_t *event)
{
    (void)event;
    if (s_callbacks.forget_wifi) s_callbacks.forget_wifi(s_callbacks.context);
}

static void refresh_event(lv_event_t *event)
{
    (void)event;
    if (s_callbacks.request_refresh) s_callbacks.request_refresh(s_callbacks.context);
    app_ui_show_dashboard(&s_config);
}

static void dashboard_refresh_event(lv_event_t *event)
{
    (void)event;
    if (s_callbacks.request_refresh) s_callbacks.request_refresh(s_callbacks.context);
}

static lv_obj_t *settings_row(lv_obj_t *parent, const char *title, const char *value, lv_event_cb_t event)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 440, 60);
    lv_obj_set_style_bg_color(button, color(COLOR_CARD), 0);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title_label = make_label(button, title, &app_font_16, COLOR_TEXT);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 4, -9);
    lv_obj_t *value_label = make_label(button, value, &app_font_12, COLOR_MUTED);
    lv_obj_align(value_label, LV_ALIGN_LEFT_MID, 4, 14);
    return button;
}

static void show_settings_event(lv_event_t *event)
{
    (void)event;
    lv_obj_t *screen = prepare_screen();
    lv_obj_t *title = make_label(screen, "Nastavení", &app_font_28, COLOR_TEXT);
    lv_obj_set_pos(title, 16, 14);
    lv_obj_t *back = make_button(screen, "Hotovo", 104, 44);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -14, 10);
    lv_obj_add_event_cb(back, dashboard_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_pos(list, 12, 70);
    lv_obj_set_size(list, 456, 398);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    settings_row(list, "Wi-Fi", s_config.has_wifi ? s_config.wifi_ssid : "Nenastavena", show_wifi_event);
    settings_row(list, "Místo", s_config.city, show_location_event);
    settings_row(list, "Časové pásmo", s_config.timezone, show_timezone_event);
    settings_row(list, "Měna BTC", s_config.btc_currency, show_currency_event);
    settings_row(list, "Claude limity",
                 s_config.has_claude_token ? "Token nastaven" : "Token není nastaven",
                 show_claude_event);
    char refresh_detail[32];
    snprintf(refresh_detail, sizeof(refresh_detail), "Počasí, BTC/%s a Claude", s_config.btc_currency);
    settings_row(list, "Obnovit data", refresh_detail, refresh_event);

    lv_obj_t *brightness_card = lv_obj_create(list);
    lv_obj_set_size(brightness_card, 440, 80);
    lv_obj_set_style_bg_color(brightness_card, color(COLOR_CARD), 0);
    lv_obj_set_style_border_width(brightness_card, 0, 0);
    lv_obj_set_style_radius(brightness_card, 14, 0);
    lv_obj_set_style_pad_all(brightness_card, 12, 0);
    make_label(brightness_card, "Jas", &app_font_14, COLOR_TEXT);
    lv_obj_t *slider = lv_slider_create(brightness_card);
    lv_obj_set_size(slider, 390, 16);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, s_config.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_event, LV_EVENT_RELEASED, NULL);

    if (s_config.has_wifi) {
        lv_obj_t *forget = make_button(list, "Zapomenout Wi-Fi", 440, 54);
        lv_obj_set_style_bg_color(forget, color(0x7F1D1D), 0);
        lv_obj_add_event_cb(forget, forget_wifi_event, LV_EVENT_CLICKED, NULL);
    }
}

void app_ui_init(const app_config_t *config, const app_ui_callbacks_t *callbacks)
{
    s_config = *config;
    s_callbacks = *callbacks;
    if (s_clock_timer == NULL) s_clock_timer = lv_timer_create(update_clock, 1000, NULL);
}
