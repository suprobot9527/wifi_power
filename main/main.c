#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dri_SSD1306.h"
#include "dri_key_ctrl.h"
#include "dri_hlw8032.h"
#include "dri_relay_control.h"
#include "int_OLED.h"
#include "int_ectric_energy.h"
#include "com_power_types.h"

// #include "int_bluetooth.h"
// #include "int_wifi.h"
// #include "int_mqtt.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "========== OLED电参数 + 按键测试 ==========");

    /* 1. 初始化继电器 */
    ESP_LOGI(TAG, "[1] 初始化继电器...");
    dri_relay_init();

    /* 2. 初始化HLW8032电能采集 */
    ESP_LOGI(TAG, "[2] 初始化HLW8032...");
    dri_hlw8032_init();

    /* 3. 初始化PF脉冲计数（累计电能） */
    ESP_LOGI(TAG, "[3] 初始化PF脉冲计数...");
    int_energy_init();

    /* 4. 初始化OLED（内部会启动刷新任务，自动显示电压/电流/功率/电能） */
    ESP_LOGI(TAG, "[4] 初始化OLED显示...");
    ESP_ERROR_CHECK(int_oled_init());

    /* 5. 初始化按键 */
    ESP_LOGI(TAG, "[5] 初始化按键...");
    key_ctrl_init();
    ESP_LOGI(TAG, "[5] 初始化完成，按键功能:");
    ESP_LOGI(TAG, "    按键1: 切换继电器开/关");
    ESP_LOGI(TAG, "    按键2: 强制关闭继电器");
    ESP_LOGI(TAG, "    按键3: 清零累计电能");
    ESP_LOGI(TAG, "    按键4: 打印当前电参数");

    /* 6. 主循环：按键处理 */
    while (1) {
        app_key_t key = key_ctrl_poll();

        switch (key) {
        case KEY_TOGGLE_RELAY: {
            bool new_state = !dri_relay_get_state();
            dri_relay_set(new_state);
            ESP_LOGI(TAG, "按键1 -> 继电器: %s", new_state ? "开" : "关");
            break;
        }
        case KEY_FORCE_OFF:
            dri_relay_set(false);
            ESP_LOGI(TAG, "按键2 -> 强制关闭继电器");
            break;

        case KEY_CLEAR_ALARM:
            int_energy_reset();
            ESP_LOGI(TAG, "按键3 -> 累计电能已清零");
            break;

        case KEY_RESERVED: {
            power_sample_t s = power_meter_get_latest();
            ESP_LOGI(TAG, "按键4 -> U=%.1fV I=%.3fA P=%.1fW E=%.2fWh valid=%d",
                     s.voltage_v, s.current_a, s.active_power_w,
                     s.energy_wh_total, s.valid);
            break;
        }
        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // ========== 以下为 BLE 测试代码（已注释）==========
    // ESP_LOGI(TAG, "========== BLE GATT Server 测试 ==========");
    // ESP_LOGI(TAG, "[1] 启动BLE GATT Server...");
    // ESP_ERROR_CHECK(int_bluetooth_init());
    // ESP_LOGI(TAG, "[1] BLE GATT Server 已启动，等待手机连接...");
    // ESP_LOGI(TAG, "    设备名: PW_CTRL");
    // ESP_LOGI(TAG, "    请用 nRF Connect 扫描并连接");
    // while (1) {
    //     ESP_LOGI(TAG, "[状态] BLE连接: %s",
    //              int_bluetooth_is_connected() ? "已连接" : "未连接");
    //     vTaskDelay(pdMS_TO_TICKS(5000));
    // }

    // ========== 以下为 WiFi + MQTT 测试代码（已注释）==========
    // /* 0. 测试阶段：每次复位都重新配网 */
    // int_wifi_reset_provisioning();
    //
    // /* 1. 初始化WiFi（BLE配网或自动连接） */
    // ESP_LOGI(TAG, "[1] 启动WiFi初始化...");
    // ESP_ERROR_CHECK(int_wifi_init());
    //
    // /* 2. 等待WiFi连接成功 */
    // ESP_LOGI(TAG, "[2] 等待WiFi连接...");
    // ESP_ERROR_CHECK(int_wifi_wait_connected(0));
    // ESP_LOGI(TAG, "[2] WiFi连接成功!");
    //
    // /* 3. 初始化MQTT（连接到 192.168.54.34:1883） */
    // ESP_LOGI(TAG, "[3] 启动MQTT连接...");
    // int_mqtt_set_data_callback(mqtt_data_cb);
    // ESP_ERROR_CHECK(int_mqtt_init(NULL));
    //
    // /* 4. 等待MQTT连接建立 */
    // ESP_LOGI(TAG, "[4] 等待MQTT连接...");
    // while (!int_mqtt_is_connected()) {
    //     vTaskDelay(pdMS_TO_TICKS(500));
    // }
    // ESP_LOGI(TAG, "[4] MQTT连接成功!");
    //
    // /* 5. 订阅主题 */
    // int_mqtt_subscribe("console_to_gateway", 1);
    // ESP_LOGI(TAG, "[5] 已订阅 console_to_gateway");
    //
    // /* 6. 循环发布测试数据 */
    // int count = 0;
    // while (1) {
    //     char msg[64];
    //     snprintf(msg, sizeof(msg), "{\"cnt\":%d,\"wifi\":%d,\"mqtt\":%d}",
    //              count++, int_wifi_is_connected(), int_mqtt_is_connected());
    //
    //     int_mqtt_publish("gateway_to_console", msg, 0, 1, 0);
    //     ESP_LOGI(TAG, "[发布] %s", msg);
    //
    //     vTaskDelay(pdMS_TO_TICKS(3000));
    // }
}