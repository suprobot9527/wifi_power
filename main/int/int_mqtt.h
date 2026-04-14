#ifndef __INT_MQTT_H__
#define __INT_MQTT_H__

#include "esp_err.h"
#include "mqtt_client.h"
#include <stdbool.h>

/**
 * @brief MQTT接收数据回调函数类型
 * @param topic   主题字符串
 * @param topic_len 主题长度
 * @param data    数据内容
 * @param data_len 数据长度
 */
typedef void (*int_mqtt_data_cb_t)(const char *topic, int topic_len,
                                   const char *data, int data_len);

/**
 * @brief 初始化并启动MQTT客户端
 * @param broker_uri  MQTT Broker地址，如 "mqtt://192.168.1.100:1883"
 * @return ESP_OK 成功
 */
esp_err_t int_mqtt_init(const char *broker_uri);

/**
 * @brief 查询MQTT是否已连接
 * @return true 已连接
 */
bool int_mqtt_is_connected(void);

/**
 * @brief 发布消息
 * @param topic  主题
 * @param data   数据
 * @param len    数据长度，0则自动strlen
 * @param qos    服务质量 0/1/2
 * @param retain 是否保留消息
 * @return msg_id (>=0成功, <0失败)
 */
int int_mqtt_publish(const char *topic, const char *data, int len, int qos, int retain);

/**
 * @brief 订阅主题
 * @param topic 主题
 * @param qos   服务质量 0/1/2
 * @return msg_id (>=0成功, <0失败)
 */
int int_mqtt_subscribe(const char *topic, int qos);

/**
 * @brief 取消订阅
 * @param topic 主题
 * @return msg_id (>=0成功, <0失败)
 */
int int_mqtt_unsubscribe(const char *topic);

/**
 * @brief 注册数据接收回调
 * @param cb 回调函数
 */
void int_mqtt_set_data_callback(int_mqtt_data_cb_t cb);

/**
 * @brief 获取MQTT客户端句柄（高级用途）
 * @return 客户端句柄，未初始化返回NULL
 */
esp_mqtt_client_handle_t int_mqtt_get_client(void);

#endif /* __INT_MQTT_H__ */
