#include "int_OLED.h"


static const char *TAG = "INT_OLED";

// OLED显示刷新周期 (ms)
#define OLED_REFRESH_PERIOD_MS  500

/**
 * @brief OLED显示刷新任务
 *        周期性读取电能数据并刷新到OLED屏幕
 */
static void oled_display_task(void *arg)
{
    char line_buf[22]; // 128/6=21字符(12号字体), 预留结尾

    while (1) {
        // 获取最新电能采样数据
        power_sample_t sample = power_meter_get_latest();

        // 清空缓冲区（不刷新屏幕）
        dri_oled_clear_buf();

        if (!sample.valid) {
            // 未收到数据时使用默认测试值
            sample.voltage_v = 220.0f;
            sample.current_a = 0.450f;
            sample.active_power_w = 99.0f;
            sample.energy_wh_total = 12.50f;
        }

        // 第1行: 电压 (y=0)
        snprintf(line_buf, sizeof(line_buf), "U: %.1f V", sample.voltage_v);
        dri_oled_show_string_buf(0, 0, line_buf, 16);

        // 第2行: 电流 (y=16)
        snprintf(line_buf, sizeof(line_buf), "I: %.3f A", sample.current_a);
        dri_oled_show_string_buf(0, 16, line_buf, 16);

        // 第3行: 功率 (y=32)
        snprintf(line_buf, sizeof(line_buf), "P: %.1f W", sample.active_power_w);
        dri_oled_show_string_buf(0, 32, line_buf, 16);

        // 第4行: 累计电能 (y=48)
        snprintf(line_buf, sizeof(line_buf), "E: %.2f Wh", sample.energy_wh_total);
        dri_oled_show_string_buf(0, 48, line_buf, 16);

        // 统一刷新到屏幕（一次I2C传输，无闪烁）
        dri_oled_refresh();

        vTaskDelay(pdMS_TO_TICKS(OLED_REFRESH_PERIOD_MS));
    }
}

esp_err_t int_oled_init(void)
{
    // 1. 初始化SSD1306 OLED
    esp_err_t ret = dri_oled_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 显示开机画面
    dri_oled_show_string(16, 24, "Power Meter", 16);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. 创建OLED刷新任务
    xTaskCreate(oled_display_task, "oled_task", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "OLED display task started");
    return ESP_OK;
}
