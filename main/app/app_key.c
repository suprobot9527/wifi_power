#include "app_key.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dri_key_ctrl.h"
#include "dri_relay_control.h"
#include "int_ectric_energy.h"
#include "int_protection.h"
#include "com_power_types.h"
#include "app_cloud.h"
#include "string.h"
#include "esp_system.h"
#include "com_debug.h"
static const char *TAG = "app_key";

static void key_task(void *arg)
{
    vTaskDelay(2000);
    while (1)
    {
        app_key_t key = key_ctrl_poll();

        switch (key)
        {
        case KEY_TOGGLE_RELAY:
        {
            bool new_state = !dri_relay_get_state();
            dri_relay_set(new_state);
            ESP_LOGI(TAG, "按键1 -> 继电器: %s", new_state ? "开" : "关");
            break;
        }
        case KEY_FORCE_OFF:
            dri_relay_set(false);
            int_protection_reset();
            ESP_LOGI(TAG, "按键2 -> 强制关闭继电器");
            break;

        case KEY_CLEAR_ALARM:
            int_energy_reset();
            ESP_LOGI(TAG, "按键3 -> 累计电能已清零");
            break;

        case KEY_RESERVED:
            /* 预留按键，暂无功能 */
            ESP_LOGI(TAG, "按键4 -> 切换到模式");
            char op_mode[8] = {0};
            esp_err_t err = load_op_mode(op_mode, sizeof(op_mode));
            if (strcmp(op_mode, OP_MODE_WIFI) == 0){
                app_cloud_save_op_mode(OP_MODE_BLE);
                ESP_LOGI(TAG, "按键4 -> 切换到BLE模式");
            } else {
                app_cloud_save_op_mode(OP_MODE_WIFI); 
                ESP_LOGI(TAG, "按键4 -> 切换到WiFi模式");
            }
            break;
            
            
            break;

        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_key_start(void)
{
    key_ctrl_init();
    xTaskCreate(key_task, "key_task", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "按键任务已启动");
    ESP_LOGI(TAG, "  按键1: 切换继电器开/关");
    ESP_LOGI(TAG, "  按键2: 强制关闭继电器");
    ESP_LOGI(TAG, "  按键3: 清零累计电能");
    ESP_LOGI(TAG, "  按键4: 预留");
}
