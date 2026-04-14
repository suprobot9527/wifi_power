/*
 * BLE GATT Server 设备控制模块
 * 参考 ESP-IDF bleprph_wifi_coex 示例
 * 配合 int_wifi.c 中 BLE 配网使用，配网完成后 NimBLE 保持运行
 *
 * GATT 服务布局：
 *   Device Control Service (自定义128-bit UUID)
 *     ├── Relay Control   (Write)       - 写入 0x01 开 / 0x00 关
 *     ├── Power Data      (Read|Notify) - 读取电压/电流/功率/电能
 *     └── Device Status   (Read)        - 读取继电器状态 + WiFi状态
 */

#include "int_bluetooth.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "dri_relay_control.h"
#include "com_power_types.h"
#include "dri_hlw8032.h"
#include "int_wifi.h"

/* 前置声明：ESP-IDF 的头文件未导出此函数 */
void ble_store_config_init(void);

static const char *TAG = "int_ble";

/* BLE 设备名 */
#define BLE_DEVICE_NAME  "PW_CTRL"

/* BLE 连接状态 */
static bool s_ble_connected = false;
static uint16_t s_conn_handle = 0;
static uint8_t s_own_addr_type;

/* Power Data Notify 的 attribute handle，用于主动推送 */
static uint16_t s_power_data_attr_handle;

/* ========== 自定义 UUID 定义 ========== */

/* Device Control Service UUID: 12345678-1234-5678-1234-56789abcdef0 */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

/* Relay Control Characteristic UUID: ...def1 */
static const ble_uuid128_t s_chr_relay_uuid =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

/* Power Data Characteristic UUID: ...def2 */
static const ble_uuid128_t s_chr_power_uuid =
    BLE_UUID128_INIT(0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

/* Device Status Characteristic UUID: ...def3 */
static const ble_uuid128_t s_chr_status_uuid =
    BLE_UUID128_INIT(0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

/* ========== GATT 访问回调 ========== */

/**
 * @brief 继电器控制 Characteristic 回调
 *   Write: 0x01=开, 0x00=关
 *   Read:  返回当前状态
 */
static int chr_relay_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        uint8_t state = dri_relay_get_state() ? 0x01 : 0x00;
        int rc = os_mbuf_append(ctxt->om, &state, sizeof(state));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint8_t val;
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len != 1) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        int rc = ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        ESP_LOGI(TAG, "BLE控制继电器: %s", val ? "开" : "关");
        dri_relay_set(val ? true : false);
        return 0;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/**
 * @brief 功率数据 Characteristic 回调
 *   Read: 返回 voltage(4B) + current(4B) + power(4B) + energy(4B) = 16字节 float
 */
static int chr_power_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    power_sample_t sample = power_meter_get_latest();
    /* 打包为4个float: voltage, current, power, energy */
    float data[4] = {
        sample.voltage_v,
        sample.current_a,
        sample.active_power_w,
        sample.energy_wh_total
    };

    int rc = os_mbuf_append(ctxt->om, data, sizeof(data));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/**
 * @brief 设备状态 Characteristic 回调
 *   Read: 返回 relay_state(1B) + wifi_connected(1B) = 2字节
 */
static int chr_status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t status[2] = {
        dri_relay_get_state() ? 0x01 : 0x00,
        int_wifi_is_connected() ? 0x01 : 0x00,
    };

    int rc = os_mbuf_append(ctxt->om, status, sizeof(status));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ========== GATT 服务定义表 ========== */

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   /* 继电器控制 */
                .uuid = &s_chr_relay_uuid.u,
                .access_cb = chr_relay_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {   /* 功率数据 */
                .uuid = &s_chr_power_uuid.u,
                .access_cb = chr_power_access_cb,
                .val_handle = &s_power_data_attr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {   /* 设备状态 */
                .uuid = &s_chr_status_uuid.u,
                .access_cb = chr_status_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }, /* 结束标记 */
        },
    },
    { 0 }, /* 结束标记 */
};

/* ========== GAP 事件处理 ========== */

static void ble_advertise(void);

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "BLE客户端已连接, handle=%d", event->connect.conn_handle);
            s_ble_connected = true;
            s_conn_handle = event->connect.conn_handle;
        } else {
            ESP_LOGW(TAG, "BLE连接失败, status=%d", event->connect.status);
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE客户端断开, reason=%d", event->disconnect.reason);
        s_ble_connected = false;
        s_conn_handle = 0;
        ble_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGD(TAG, "广播完成, 重新开始");
        ble_advertise();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU更新: conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "订阅事件: attr_handle=%d, notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        break;

    default:
        break;
    }
    return 0;
}

/**
 * @brief 开始BLE广播
 */
static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "广播数据设置失败, rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "广播启动失败, rc=%d", rc);
    }
}

/* ========== NimBLE Host 回调 ========== */

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE Host重置, reason=%d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "无法确定地址类型, rc=%d", rc);
        return;
    }

    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "BLE地址: %02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    ble_advertise();
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host任务启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "注册服务 %s, handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "注册特征 %s, def_handle=%d, val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "注册描述符 %s, handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;
    default:
        break;
    }
}

/* ========== Notify 定时推送任务 ========== */

static void ble_notify_task(void *param)
{
    static uint32_t test_cnt = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!s_ble_connected || s_power_data_attr_handle == 0) {
            continue;
        }

        /* 构造测试消息: 4字节计数值 */
        test_cnt++;
        char test_msg[32];
        int len = snprintf(test_msg, sizeof(test_msg), "BLE_TEST:%lu", (unsigned long)test_cnt);

        struct os_mbuf *om = ble_hs_mbuf_from_flat(test_msg, len);
        if (om == NULL) {
            ESP_LOGE(TAG, "Notify mbuf分配失败");
            continue;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_power_data_attr_handle, om);
        if (rc == 0) {
            ESP_LOGI(TAG, "[Notify] %s", test_msg);
        } else {
            ESP_LOGW(TAG, "Notify发送失败, rc=%d", rc);
        }
    }
}

/* ========== 外部接口 ========== */

esp_err_t int_bluetooth_init(void)
{
    int rc;

    /* 初始化 NimBLE 协议栈 */
    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE初始化失败, rc=%d", rc);
        return ESP_FAIL;
    }

    /* 配置 NimBLE Host */
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* 初始化 GATT 服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT计数失败, rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT添加服务失败, rc=%d", rc);
        return ESP_FAIL;
    }

    /* 设置BLE设备名 */
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    assert(rc == 0);

    /* 初始化存储 */
    ble_store_config_init();

    /* 启动 Host 任务 */
    nimble_port_freertos_init(ble_host_task);

    /* 启动 Notify 定时推送任务 */
    xTaskCreate(ble_notify_task, "ble_notify", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "BLE控制服务已启动, 设备名: %s", BLE_DEVICE_NAME);
    return ESP_OK;
}

bool int_bluetooth_is_connected(void)
{
    return s_ble_connected;
}
