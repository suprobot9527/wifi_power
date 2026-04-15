#include "app_system.h"
#include "esp_log.h"
#include "dri_relay_control.h"
#include "dri_hlw8032.h"
#include "int_ectric_energy.h"
#include "int_OLED.h"
#include "int_protection.h"
#include "dri_key_ctrl.h"
#include "int_wifi.h"
#include "int_mqtt.h"
#include "app_key.h"
#include "app_cloud.h"

static const char *TAG = "app_system";






void app_system_start(void)
{


    ESP_LOGI(TAG, "========== 智能电源系统启动 ==========");

    /* 1. 硬件驱动初始化 */
    ESP_LOGI(TAG, "[1/7] 初始化继电器...");
    dri_relay_init();

    ESP_LOGI(TAG, "[2/7] 初始化HLW8032电能采集...");
    dri_hlw8032_init();

    /* 2. 中间层初始化 */
    ESP_LOGI(TAG, "[3/7] 初始化PF脉冲计数...");
    int_energy_init();

    ESP_LOGI(TAG, "[4/7] 初始化保护模块...");
    int_protection_init();

    ESP_LOGI(TAG, "[5/7] 初始化OLED显示...");
    ESP_ERROR_CHECK(int_oled_init());

    /* 3. 应用层任务启动 */
    ESP_LOGI(TAG, "[6/7] 启动按键处理任务...");
    app_key_start();

    // app_cloud_reset(); // 重置云端连接状态，确保每次启动都从WiFi配网开始

    ESP_LOGI(TAG, "[7/7] 启动云端连接任务...");
    app_cloud_start();

    ESP_LOGI(TAG, "========== 系统启动完成 ==========");
}
