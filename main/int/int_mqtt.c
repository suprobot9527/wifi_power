/*
 * MQTT客户端模块（TCP明文）
 * 参考 ESP-IDF mqtt/tcp 示例
 */

#include "int_mqtt.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "int_mqtt";

/* MQTT客户端句柄 */
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* 默认Broker地址 */
#define MQTT_BROKER_URI_DEFAULT "mqtt://192.168.137.1:1883"

/* 连接状态 */
static bool s_mqtt_connected = false;

/* 用户数据接收回调 */
static int_mqtt_data_cb_t s_data_callback = NULL;

/* ============== 内部函数 ============== */

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/**
 * @brief MQTT事件处理函数
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT已连接到Broker");
        s_mqtt_connected = true;
        /* TODO: 在此处添加连接后的订阅操作 */
        /* 例如:
         * int_mqtt_subscribe("/device/控制主题", 1);
         */
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT连接断开");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "订阅成功, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "取消订阅成功, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "发布成功, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "收到数据: topic=%.*s", event->topic_len, event->topic);
        /* 调用用户回调 */
        if (s_data_callback) {
            s_data_callback(event->topic, event->topic_len,
                            event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT错误");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "errno: %s", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGD(TAG, "其他事件: id=%d", event->event_id);
        break;
    }
}

/* ============== 外部接口 ============== */

esp_err_t int_mqtt_init(const char *broker_uri)
{
    if (s_mqtt_client != NULL) {
        ESP_LOGW(TAG, "MQTT已初始化，跳过");
        return ESP_OK;
    }

    if (broker_uri == NULL) {
        broker_uri = MQTT_BROKER_URI_DEFAULT;
    }

    ESP_LOGI(TAG, "初始化MQTT, Broker: %s", broker_uri);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT客户端创建失败");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    return ESP_OK;
}

bool int_mqtt_is_connected(void)
{
    return s_mqtt_connected;
}

int int_mqtt_publish(const char *topic, const char *data, int len, int qos, int retain)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT未初始化");
        return -1;
    }
    if (len == 0 && data != NULL) {
        len = strlen(data);
    }
    return esp_mqtt_client_publish(s_mqtt_client, topic, data, len, qos, retain);
}

int int_mqtt_subscribe(const char *topic, int qos)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT未初始化");
        return -1;
    }
    return esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
}

int int_mqtt_unsubscribe(const char *topic)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT未初始化");
        return -1;
    }
    return esp_mqtt_client_unsubscribe(s_mqtt_client, topic);
}

void int_mqtt_set_data_callback(int_mqtt_data_cb_t cb)
{
    s_data_callback = cb;
}

esp_mqtt_client_handle_t int_mqtt_get_client(void)
{
    return s_mqtt_client;
}

void int_mqtt_stop(void)
{
    if (s_mqtt_client != NULL) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_mqtt_connected = false;
        ESP_LOGI(TAG, "MQTT客户端已停止并销毁");
    }
}
