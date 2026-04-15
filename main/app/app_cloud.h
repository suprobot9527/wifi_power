#ifndef __APP_CLOUD_H__
#define __APP_CLOUD_H__

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "int_wifi.h"
#include "int_mqtt.h"
#include "int_bluetooth.h"
#include "dri_hlw8032.h"
#include "dri_relay_control.h"
#include "int_ectric_energy.h"
#include "int_protection.h"
#include "com_power_types.h"

#include "nvs_flash.h"
#include "nvs.h"

#define OP_MODE_WIFI    "wifi"
#define OP_MODE_BLE     "ble"
#define MODE_NAMESPACE "app_config"
#define MODE_KEY       "op_mode"

/**
 * @brief 启动云端连接任务
 *        自动完成：WiFi配网/连接 → MQTT连接 → 周期上报电参数 → 接收远程指令
 */
void app_cloud_start(void);

/**
 * @brief 重置云端连接
 *        停止当前任务 → 断开MQTT → 重置WiFi重连状态 → 重新启动云端任务
 */
void app_cloud_reset(void);

/**
 * @brief 保存工作模式
 * @param mode 工作模式字符串 ("wifi" 或 "ble")
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t app_cloud_save_op_mode(const char *mode);

/**
 * @brief 加载工作模式
 * @param buf 存储工作模式的缓冲区
 * @param buf_size 缓冲区大小
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t load_op_mode(char *buf, size_t buf_size);

#endif /* __APP_CLOUD_H__ */
