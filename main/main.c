#include <stdio.h>
#include "app_system.h"
#include "nvs_flash.h"  // NVS 闪存初始化函数
#include "esp_err.h"    // ESP_ERROR_CHECK 宏和错误码定义
//{"relay":1,"reset_energy":0,"overcurrent":0,"overvoltage":0,"undervoltage":0,"leakage":0}
void app_main(void)
{
     // 1. 初始化 NVS（必须最先）
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 若分区被占用或版本升级，擦除并重试
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    app_system_start();
}
