/*
 * WiFi初始化 + BLE蓝牙配网模块（Security 0 + 二维码 + custom-data）
 * 参考 ESP-IDF wifi_provisioning 示例，使用 BLE 作为配网传输通道
 * 注意：配网完成后不释放BLE资源，保留给后续蓝牙控制使用
 */

#include "int_wifi.h"

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include "qrcode.h"

static const char *TAG = "int_wifi";

/* 事件标志位 */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_DISCONNECTED_BIT BIT1

/* 是否需要重置配网（由 int_wifi_reset_provisioning 设置） */
static bool s_need_reset_prov = false;

static EventGroupHandle_t s_wifi_event_group;
static bool s_wifi_connected = false;

/* 配网失败重试次数 */
#define PROV_MGR_MAX_RETRY  5
static int s_prov_retry_count = 0;

/* WiFi断连重试计数 */
#define WIFI_RECONNECT_MAX  10
static int s_reconnect_count = 0;
static bool s_wifi_gave_up = false;  /* WiFi重连放弃标志 */

/* 二维码相关 */
#define PROV_QR_VERSION     "v1"
#define PROV_TRANSPORT_BLE  "ble"
#define QRCODE_BASE_URL     "https://espressif.github.io/esp-jumpstart/qrcode.html"

/* ============== 内部函数 ============== */

/* 前向声明 */
static esp_err_t wifi_start_provisioning(void);
static void wifi_start_sta(void);

/**
 * @brief 生成BLE设备名称（基于MAC地址后3字节）
 */
static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PW_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

/**
 * @brief 系统事件处理函数
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    /* ---- 配网事件 ---- */
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "配网服务已启动");
            break;
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "收到WiFi凭据 SSID:%s", (const char *)cfg->ssid);
            break;
        }
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "配网失败: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR)
                         ? "WiFi认证失败"
                         : "未找到AP");
            s_prov_retry_count++;
            if (s_prov_retry_count >= PROV_MGR_MAX_RETRY) {
                ESP_LOGI(TAG, "重试次数已达上限，重置配网状态");
                wifi_prov_mgr_reset_sm_state_on_failure();
                s_prov_retry_count = 0;
            }
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "配网成功");
            s_prov_retry_count = 0;
            break;
        case WIFI_PROV_END:
            /* 配网完成后释放配网管理器资源 */
            wifi_prov_mgr_deinit();
            break;
        default:
            break;
        }
    }
    /* ---- WiFi STA 事件 ---- */
    else if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_wifi_connected = false;
            xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            s_reconnect_count++;
            ESP_LOGW(TAG, "WiFi断开，尝试重连(%d/%d)...", s_reconnect_count, WIFI_RECONNECT_MAX);
            if (s_reconnect_count >= WIFI_RECONNECT_MAX) {
                ESP_LOGW(TAG, "重连%d次失败，放弃WiFi连接", WIFI_RECONNECT_MAX);
                s_wifi_gave_up = true;
                esp_wifi_stop();
            } else {
                esp_wifi_connect();
            }
            break;
        default:
            break;
        }
    }
    /* ---- IP 事件 ---- */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        s_reconnect_count = 0;  /* 连接成功，重置重连计数 */
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
    }
    /* ---- BLE 传输事件 ---- */
    else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
        case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
            ESP_LOGI(TAG, "BLE已连接");
            break;
        case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
            ESP_LOGI(TAG, "BLE已断开");
            break;
        default:
            break;
        }
    }
    /* ---- 安全会话事件 ---- */
    else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "安全会话建立成功");
            break;
        case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
            ESP_LOGE(TAG, "安全参数无效");
            break;
        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGE(TAG, "凭据不匹配");
            break;
        default:
            break;
        }
    }
}

/**
 * @brief 启动WiFi STA模式
 */
static void wifi_start_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief custom-data端点处理函数，可接收App发送的自定义数据
 */
static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "收到自定义数据: %.*s", inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "内存不足");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1;
    return ESP_OK;
}

/**
 * @brief 在串口终端打印配网二维码
 */
static void wifi_prov_print_qr(const char *name, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "无法生成二维码，参数缺失");
        return;
    }
    char payload[150] = {0};
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"%s\",\"name\":\"%s\",\"transport\":\"%s\"}",
             PROV_QR_VERSION, name, transport);

    ESP_LOGI(TAG, "请使用 ESP BLE Provisioning App 扫描此二维码进行配网:");
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);

    ESP_LOGI(TAG, "若二维码不可见，请在浏览器中打开:\n%s?data=%s", QRCODE_BASE_URL, payload);
}

/**
 * @brief 启动BLE配网流程
 */
static esp_err_t wifi_start_provisioning(void)
{
    /* 配置配网管理器：使用BLE方案
     * 使用 WIFI_PROV_EVENT_HANDLER_NONE：配网完成后不释放BLE资源
     * 这样后续可以继续使用BLE进行设备控制 */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    /* 让出CPU，避免看门狗超时 */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 检查是否已配网 */
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        ESP_LOGI(TAG, "设备未配网，启动BLE配网...");

        /* 生成BLE广播设备名 */
        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        /* 设置自定义BLE服务UUID */
        uint8_t custom_service_uuid[] = {
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        };
        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

        /* 创建custom-data自定义数据端点（必须在start之前） */
        wifi_prov_mgr_endpoint_create("custom-data");

        /* 启动配网服务: Security 0（明文，无需PoP验证码） */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_0, NULL, service_name, NULL));

        /* 注册custom-data端点回调（必须在start之后） */
        wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);

        ESP_LOGI(TAG, "BLE设备名: %s", service_name);

        /* 让出CPU避免看门狗超时 */
        vTaskDelay(pdMS_TO_TICKS(100));

        /* 打印二维码到串口终端 */
        wifi_prov_print_qr(service_name, PROV_TRANSPORT_BLE);

        /* 二维码打印完成后让出CPU */
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        ESP_LOGI(TAG, "设备已配网，直接连接WiFi");
        /* 已配网则释放配网管理器，直接启动STA */
        wifi_prov_mgr_deinit();
        wifi_start_sta();
    }

    return ESP_OK;
}

/* ============== 外部接口 ============== */

esp_err_t int_wifi_init(void)
{
    /* 初始化NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 初始化TCP/IP协议栈 */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 创建默认事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 创建事件组 */
    s_wifi_event_group = xEventGroupCreate();

    /* 注册事件处理函数 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /* 创建默认WiFi STA网络接口 */
    esp_netif_create_default_wifi_sta();

    /* 初始化WiFi驱动 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* ESP32-C3单核，WiFi+BLE初始化耗时较长，主动让出CPU避免触发看门狗 */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 如果标记了重置，在WiFi初始化完成后执行 */
    if (s_need_reset_prov) {
        wifi_prov_mgr_config_t rst_config = {
            .scheme = wifi_prov_scheme_ble,
            .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        };
        ESP_ERROR_CHECK(wifi_prov_mgr_init(rst_config));
        wifi_prov_mgr_reset_provisioning();
        wifi_prov_mgr_deinit();
        ESP_LOGI(TAG, "配网信息已重置");
        s_need_reset_prov = false;
    }

    /* 启动配网流程 */
    return wifi_start_provisioning();
}

bool int_wifi_is_connected(void)
{
    return s_wifi_connected;
}

esp_err_t int_wifi_wait_connected(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           false, true, ticks);
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t int_wifi_reset_provisioning(void)
{
    /* 设置标记，在 int_wifi_init() 中WiFi驱动就绪后执行实际重置 */
    s_need_reset_prov = true;
    ESP_LOGI(TAG, "已标记重置配网，将在初始化时生效");
    return ESP_OK;
}

esp_err_t int_wifi_reconnect_reset(void)
{
    ESP_LOGI(TAG, "重置WiFi重连状态，重新尝试连接...");
    s_reconnect_count = 0;
    s_wifi_gave_up = false;
    s_wifi_connected = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    /* 先停止再启动WiFi，触发STA_START事件自动连接 */
    esp_wifi_stop();
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi重启失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

bool int_wifi_gave_up(void)
{
    return s_wifi_gave_up;
}
