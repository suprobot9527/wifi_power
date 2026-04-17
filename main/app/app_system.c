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

    /* 7. 云端连接（最后启动，确保所有数据就绪） */
    ESP_LOGI(TAG, "[7/7] 启动云端连接任务...");
    app_cloud_start();

    /* 1. 硬件驱动初始化（优先保障安全状态） */
    ESP_LOGI(TAG, "[1/7] 初始化继电器...");
    dri_relay_init();

    /* 2. 提前配置 PF 脉冲中断（防止 HLW8032 上电后脉冲丢失） */
    ESP_LOGI(TAG, "[2/7] 初始化 PF 脉冲计数（GPIO中断）...");
    int_energy_init();

    /* 3. HLW8032 UART 初始化（芯片开始工作，脉冲进入中断计数） */
    ESP_LOGI(TAG, "[3/7] 初始化 HLW8032 电能采集...");
    dri_hlw8032_init();

    /* 4. 保护模块（依赖电能数据） */
    ESP_LOGI(TAG, "[4/7] 初始化保护模块...");
    int_protection_init();

    /* 5. OLED 显示 */
    ESP_LOGI(TAG, "[5/7] 初始化 OLED 显示...");
    ESP_ERROR_CHECK(int_oled_init());

    /* 6. 应用层任务启动 */
    ESP_LOGI(TAG, "[6/7] 启动按键处理任务...");
    app_key_start();

    

    ESP_LOGI(TAG, "========== 系统启动完成 ==========");
}