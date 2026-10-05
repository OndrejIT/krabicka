#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*app_web_claude_token_callback_t)(const char *token, void *context);

esp_err_t app_web_start(app_web_claude_token_callback_t callback, void *context);
void app_web_stop(void);
void app_web_enable_claude_setup(uint32_t seconds);
bool app_web_claude_setup_enabled(void);
void app_web_set_token_configured(bool configured);

