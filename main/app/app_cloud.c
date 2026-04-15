#include "app_cloud.h"


static const char *TAG = "app_cloud";

// app_cloud.c 头部添加



 esp_err_t load_op_mode(char *buf, size_t buf_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t required_size = buf_size;
    err = nvs_get_str(handle, MODE_KEY, buf, &required_size);
    nvs_close(handle);
    return err;
}

// 供外部按键回调调用的保存函数（需要在 app_cloud.h 中声明）
esp_err_t app_cloud_save_op_mode(const char *mode)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, MODE_KEY, mode);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}




/* MQTT 主题定义 */
#define TOPIC_UPLOAD "gateway_to_console" /* 设备上报电参数 */
#define TOPIC_CMD "console_to_gateway"    /* 接收远程控制指令 */

/* 上报周期 (ms) */
#define CLOUD_REPORT_PERIOD_MS 5000

/**
 * @brief MQTT 数据接收回调 —— 处理远程指令
 *        固定格式 JSON:
 *        {
 *          "relay": 1,            // 1=开, 0=关, -1=不操作
 *          "reset_energy": 0,     // 1=清零, 0=不操作
 *          "overcurrent": 10.0,   // 过流阈值(A),  0=不修改
 *          "overvoltage": 260.0,  // 过压阈值(V),  0=不修改
 *          "undervoltage": 180.0, // 欠压阈值(V),  0=不修改
 *          "leakage": 30.0        // 漏电阈值(mA), 0=不修改
 *        }
 */
static void mqtt_cmd_cb(const char *topic, int topic_len,
                        const char *data, int data_len)
{
    ESP_LOGI(TAG, "收到指令: %.*s", data_len, data);

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL)
    {
        ESP_LOGW(TAG, "JSON解析失败");
        return;
    }

    /* 继电器控制: 1=开, 0=关, -1=不操作 */
    cJSON *relay = cJSON_GetObjectItem(root, "relay");
    if (cJSON_IsNumber(relay))
    {
        int val = relay->valueint;
        if (val == 1)
        {
            dri_relay_set(true);
            ESP_LOGI(TAG, "远程指令 -> 继电器: 开");
        }
        else if (val == 0)
        {
            dri_relay_set(false);
            ESP_LOGI(TAG, "远程指令 -> 继电器: 关");
        }
    }

    /* 清零电能: 1=清零, 0=不操作 */
    cJSON *reset = cJSON_GetObjectItem(root, "reset_energy");
    if (cJSON_IsNumber(reset) && reset->valueint == 1)
    {
        int_energy_reset();
        ESP_LOGI(TAG, "远程指令 -> 累计电能已清零");
    }

    /* 保护阈值: >0 则更新, 0=不修改 */
    protect_threshold_t th = int_protection_get_threshold();
    bool th_changed = false;

    cJSON *oc = cJSON_GetObjectItem(root, "overcurrent");
    if (cJSON_IsNumber(oc) && oc->valuedouble > 0)
    {
        th.overcurrent_a = (float)oc->valuedouble;
        th_changed = true;
    }

    cJSON *ov = cJSON_GetObjectItem(root, "overvoltage");
    if (cJSON_IsNumber(ov) && ov->valuedouble > 0)
    {
        th.overvoltage_v = (float)ov->valuedouble;
        th_changed = true;
    }

    cJSON *uv = cJSON_GetObjectItem(root, "undervoltage");
    if (cJSON_IsNumber(uv) && uv->valuedouble > 0)
    {
        th.undervoltage_v = (float)uv->valuedouble;
        th_changed = true;
    }

    cJSON *lk = cJSON_GetObjectItem(root, "leakage");
    if (cJSON_IsNumber(lk) && lk->valuedouble > 0)
    {
        th.leakage_ma = (float)lk->valuedouble;
        th_changed = true;
    }

    if (th_changed)
    {
        int_protection_set_threshold(&th);
        ESP_LOGI(TAG, "远程指令 -> 阈值更新: OC=%.1fA OV=%.1fV UV=%.1fV LK=%.1fmA",
                 th.overcurrent_a, th.overvoltage_v, th.undervoltage_v, th.leakage_ma);
    }

    cJSON_Delete(root);
}

/**
 * @brief 上报电参数到 MQTT
 */
static void cloud_publish_power(void)
{
    power_sample_t s = power_meter_get_latest();
    protect_state_t ps = int_protection_get_state();

    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"voltage\":%.1f,\"current\":%.3f,\"power\":%.1f,"
             "\"energy\":%.2f,\"relay\":%d,\"protect\":%d,\"valid\":%d}",
             s.voltage_v, s.current_a, s.active_power_w,
             s.energy_wh_total,
             dri_relay_get_state() ? 1 : 0,
             ps.tripped ? (int)ps.reason : 0,
             s.valid ? 1 : 0);

    int_mqtt_publish(TOPIC_UPLOAD, buf, 0, 1, 0);
    ESP_LOGD(TAG, "上报: %s", buf);
}

/**
 * @brief 云端连接任务
 *        流程: WiFi连接 → MQTT连接 → 周期上报
 *        WiFi断连10次后自动切换到BLE传输模式
 */
static void cloud_task(void *arg)
{
    /* 1. WiFi 初始化与连接 */
    ESP_LOGI(TAG, "启动WiFi...");
    esp_err_t ret = int_wifi_init();
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "WiFi初始化失败, 切换到BLE模式");
    //     goto switch_to_ble;
    // }

    /* 等待WiFi连接，超时60秒 */
    ESP_LOGI(TAG, "等待WiFi连接(超时60s)...");
    ret = int_wifi_wait_connected(60000);
    // if (ret != ESP_OK || int_wifi_gave_up()) {
    //     ESP_LOGW(TAG, "WiFi连接失败, 切换到BLE模式");
    //     goto switch_to_ble;
    // }
    ESP_LOGI(TAG, "WiFi已连接");

    /* 2. MQTT 初始化与连接 */
    int_mqtt_set_data_callback(mqtt_cmd_cb);
    ret = int_mqtt_init(NULL);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "MQTT初始化失败, 切换到BLE模式");
    //     goto switch_to_ble;
    // }

    ESP_LOGI(TAG, "等待MQTT连接...");
    for (int i = 0; i < 30; i++)
    {
        if (int_mqtt_is_connected())
            break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!int_mqtt_is_connected())
    {
        ESP_LOGW(TAG, "MQTT连接超时, 切换到BLE模式");
        // goto switch_to_ble;
    }
    ESP_LOGI(TAG, "MQTT已连接");

    /* 3. 订阅远程控制主题 */
    int_mqtt_subscribe(TOPIC_CMD, 1);
    ESP_LOGI(TAG, "已订阅: %s", TOPIC_CMD);

    /* 4. MQTT模式：周期上报，监控WiFi状态 */
    while (1)
    {
        /* 如果WiFi断连10次放弃，则退出MQTT模式 */
        // if (int_wifi_gave_up()) {
        //     ESP_LOGW(TAG, "WiFi已断开且放弃重连, 切换到BLE模式");
        //     goto switch_to_ble;
        // }
        if (int_mqtt_is_connected())
        {
            cloud_publish_power();
        }
        vTaskDelay(pdMS_TO_TICKS(CLOUD_REPORT_PERIOD_MS));
    }

    // switch_to_ble:
    //     /* 切换到BLE传输模式 */
    //     ESP_LOGI(TAG, "========== 切换到BLE传输模式 ==========");
    //     ret = int_bluetooth_init();
    //     if (ret != ESP_OK) {
    //         ESP_LOGE(TAG, "BLE初始化失败: %s", esp_err_to_name(ret));
    //     } else {
    //         ESP_LOGI(TAG, "BLE GATT Server已启动, 设备名: PW_CTRL");
    //         ESP_LOGI(TAG, "请使用手机APP通过蓝牙连接设备");
    //     }
    //     /* BLE模式下由 int_bluetooth 内部的 notify_task 周期推送数据 */
    //     vTaskDelete(NULL);
}

void app_cloud_bluetooth_init(void)
{
        /* 切换到BLE传输模式 */
        ESP_LOGI(TAG, "========== 切换到BLE传输模式 ==========");
        esp_err_t ret = int_bluetooth_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "BLE初始化失败: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "BLE GATT Server已启动, 设备名: PW_CTRL");
            ESP_LOGI(TAG, "请使用手机APP通过蓝牙连接设备");
        }
        /* BLE模式下由 int_bluetooth 内部的 notify_task 周期推送数据 */
        // vTaskDelete(NULL);
}

void app_cloud_start(void)
{
    char op_mode[8] = {0};
    esp_err_t err = load_op_mode(op_mode, sizeof(op_mode));
    
    if (err != ESP_OK || strlen(op_mode) == 0) {
        ESP_LOGI(TAG, "首次启动或模式配置不存在，默认使用 BLE 模式");
        strcpy(op_mode, OP_MODE_BLE);
        app_cloud_save_op_mode(OP_MODE_BLE);
    }

    if (strcmp(op_mode, OP_MODE_WIFI) == 0) {
        ESP_LOGI(TAG, "检测到工作模式：WiFi + MQTT");
        xTaskCreate(cloud_task, "cloud_task", 8192, NULL, 4, NULL);
    } else {
        ESP_LOGI(TAG, "检测到工作模式：BLE");
        app_cloud_bluetooth_init();
    }
    ESP_LOGI(TAG, "通信模式启动完成");
}

void app_cloud_reset(void)
{
    ESP_LOGW(TAG, "========== 重置WiFi配网，准备重启 ==========");
    int_wifi_reset_provisioning();
    vTaskDelay(pdMS_TO_TICKS(500));
}
