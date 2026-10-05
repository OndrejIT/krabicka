#pragma once

#include "app_model.h"
#include "esp_err.h"

void app_config_set_defaults(app_config_t *config);
esp_err_t app_storage_init(void);
esp_err_t app_storage_load(app_config_t *config);
esp_err_t app_storage_save(const app_config_t *config);
esp_err_t app_storage_forget_wifi(app_config_t *config);
