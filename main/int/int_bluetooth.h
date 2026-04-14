#ifndef __INT_BLUETOOTH_H__
#define __INT_BLUETOOTH_H__

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief 初始化BLE GATT Server（设备控制服务）
 *
 * 在WiFi配网完成后调用，利用已保留的NimBLE协议栈
 * 创建自定义GATT Service，提供：
 *   - 继电器控制 (Write)
 *   - 功率数据读取 (Read/Notify)
 *   - 设备状态读取 (Read)
 *
 * @return ESP_OK 成功
 */
esp_err_t int_bluetooth_init(void);

/**
 * @brief 查询BLE是否有客户端连接
 * @return true 有连接
 */
bool int_bluetooth_is_connected(void);

#endif /* __INT_BLUETOOTH_H__ */
