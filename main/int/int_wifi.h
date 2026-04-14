#ifndef __INT_WIFI_H__
#define __INT_WIFI_H__

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief 初始化WiFi并启动BLE配网
 *
 * 功能说明：
 * - 初始化NVS、netif、事件循环
 * - 检查是否已配网，若未配网则启动BLE配网服务
 * - 若已配网则直接连接WiFi
 *
 * @return ESP_OK 成功
 */
esp_err_t int_wifi_init(void);

/**
 * @brief 查询WiFi是否已连接
 * @return true 已连接, false 未连接
 */
bool int_wifi_is_connected(void);

/**
 * @brief 等待WiFi连接成功（阻塞）
 * @param timeout_ms 超时时间(ms)，0表示永久等待
 * @return ESP_OK 连接成功, ESP_ERR_TIMEOUT 超时
 */
esp_err_t int_wifi_wait_connected(uint32_t timeout_ms);

/**
 * @brief 重置配网信息，下次启动将重新进入配网模式
 * @return ESP_OK 成功
 */
esp_err_t int_wifi_reset_provisioning(void);

#endif /* __INT_WIFI_H__ */
