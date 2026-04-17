#include "dri_hlw8032.h"
#include "int_protection.h"
#include "dri_relay_control.h"
// 存储最新的电能采样数据
static power_sample_t s_latest = {0};

static const char *HLW8032_TAG = "hlw8032_uart";
static QueueHandle_t hlw8032_uart_queue;

// 帧扫描缓冲区（新增）
#define FRAME_SCAN_BUF_SIZE 128
static uint8_t s_scan_buf[FRAME_SCAN_BUF_SIZE];
static int s_scan_len = 0;

/**
 * @brief 从3字节大端数据中提取24位无符号整数
 */
static uint32_t hlw8032_get_uint24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/**
 * @brief 计算校验和（byte2 ~ byte22 之和的低8位）
 */
static uint8_t hlw8032_calc_checksum(const uint8_t *buf)
{
    uint32_t sum = 0;
    for (int i = 2; i <= 22; i++)
    {
        sum += buf[i];
    }
    return (uint8_t)(sum & 0xFF);
}

/**
 * @brief 在缓冲区中查找有效帧（滑动窗口扫描）
 * @return 帧起始索引，-1 表示未找到
 */
static int hlw8032_find_frame(const uint8_t *buf, int len)
{
    for (int i = 0; i <= len - 24; i++)
    {
        // 第一字节应为 0x55（或 0xF2 等，可根据需要放宽）
        // 第二字节固定为 0x5A（更稳定的同步标志）
        if (buf[i] != 0x55 && buf[i] != 0xF2)
            continue;
        if (buf[i + 1] != 0x5A)
            continue;
        if (hlw8032_calc_checksum(&buf[i]) != buf[i + 23])
            continue;
        return i;
    }
    return -1;
}

/**
 * @brief 从24字节数据帧中提取电压、电流和功率信息（保持不变）
 */
static bool dri_hlw8032_extract_data(const uint8_t *buf, power_sample_t *out)
{
    if (buf[0] != 0x55)
        return false; // 仍然要求 0x55（校准正常）

    const uint32_t v_param = hlw8032_get_uint24(&buf[2]);
    const uint32_t v_reg = hlw8032_get_uint24(&buf[5]);
    const uint32_t i_param = hlw8032_get_uint24(&buf[9]);
    const uint32_t i_reg = hlw8032_get_uint24(&buf[12]);
    const uint32_t p_param = hlw8032_get_uint24(&buf[16]);
    const uint32_t p_reg = hlw8032_get_uint24(&buf[19]);

    if (v_reg != 0)
    {
        out->voltage_v = ((float)v_param / (float)v_reg) * APP_CALIB_VOLTAGE_GAIN;
    }

    // 电流保护
    if (i_reg > 100000)
    {
        float raw_i = (float)i_param / (float)i_reg;
        if (raw_i >= 0.0f && raw_i < 20.0f)
        {
            out->current_a = raw_i * APP_CALIB_CURRENT_GAIN;
        }
        else
        {
            out->current_a = 0.0f;
        }
    }
    else
    {
        out->current_a = 0.0f;
    }

    // 功率保护
    if (out->current_a > 0.01f && p_reg > 1000)
    {
        float raw_p = (float)p_param / (float)p_reg;
        if (raw_p >= 0.0f && raw_p < 5000.0f)
        {
            out->active_power_w = raw_p * APP_CALIB_POWER_GAIN;
        }
        else
        {
            out->active_power_w = 0.0f;
        }
    }
    else
    {
        out->active_power_w = 0.0f;
    }

    out->valid = true;
    out->timestamp_ms = esp_timer_get_time() / 1000ULL;
    return true;
}

/**
 * @brief 处理接收到的原始字节流（批量解析）
 */
static void hlw8032_process_bytes(const uint8_t *data, int len)
{
    // 追加到扫描缓冲区
    if (s_scan_len + len > FRAME_SCAN_BUF_SIZE)
    {
        // 保留最后 23 字节（防止帧被截断）
        int keep = 23;
        if (keep > s_scan_len)
            keep = s_scan_len;
        memmove(s_scan_buf, &s_scan_buf[s_scan_len - keep], keep);
        s_scan_len = keep;
    }
    memcpy(&s_scan_buf[s_scan_len], data, len);
    s_scan_len += len;

    // 循环解析所有完整帧
    while (s_scan_len >= 24)
    {
        int idx = hlw8032_find_frame(s_scan_buf, s_scan_len);
        if (idx < 0)
        {
            // 未找到有效帧，保留最后 23 字节
            int keep = 23;
            if (keep > s_scan_len)
                keep = s_scan_len;
            memmove(s_scan_buf, &s_scan_buf[s_scan_len - keep], keep);
            s_scan_len = keep;
            break;
        }

        // 解析帧
        power_sample_t sample = s_latest;
        if (dri_hlw8032_extract_data(&s_scan_buf[idx], &sample))
        {
            sample.energy_wh_total = int_energy_get_wh();
            s_latest = sample;
            // 新增：采集到新数据后立即检测保护
            protect_state_t ps = int_protection_check(&s_latest);
            if (ps.tripped)
            {
                dri_relay_set(false); // 触发保护时断开继电器
            }
        }

        // 移除已处理的数据
        int consume = idx + 24;
        memmove(s_scan_buf, &s_scan_buf[consume], s_scan_len - consume);
        s_scan_len -= consume;
    }
}

/**
 * @brief UART 事件任务（融合版）
 */
static void dri_hlw8032_uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t data[128];
    TickType_t last_update_ticks = 0;
    const TickType_t update_interval_ticks = pdMS_TO_TICKS(1000);
    // 新增：数据不变检测
    static power_sample_t last_sample = {0};
    int same_count = 0;
    const int SAME_LIMIT = 30; // 30秒不变则重启采集

    while (1)
    {
        if (xQueueReceive(hlw8032_uart_queue, &event, pdMS_TO_TICKS(100)))
        {
            switch (event.type)
            {
            case UART_DATA:
                // 读取所有可用数据（而非固定24字节）
                while (1)
                {
                    size_t avail;
                    uart_get_buffered_data_len(HLW8032_UART_NUM, &avail);
                    if (avail == 0)
                        break;

                    int to_read = (avail > sizeof(data)) ? sizeof(data) : (int)avail;
                    int len = uart_read_bytes(HLW8032_UART_NUM, data, to_read, pdMS_TO_TICKS(50));
                    if (len > 0)
                    {
                        hlw8032_process_bytes(data, len);
                    }
                    else
                    {
                        break;
                    }
                }
                break;

            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                uart_flush_input(HLW8032_UART_NUM);
                xQueueReset(hlw8032_uart_queue);
                s_scan_len = 0; // 清空扫描缓冲区，重新同步
                ESP_LOGE(HLW8032_TAG, "UART溢出，已清空并重同步");
                break;

            default:
                break;
            }
        }

        // 1秒限流打印（每1秒打印一次最新数据）
        TickType_t now = xTaskGetTickCount();
        if ((now - last_update_ticks) >= update_interval_ticks)
        {
            if (s_latest.valid)
            {
                ESP_LOGI(HLW8032_TAG, "%.1fV %.2fA %.1fW",
                         s_latest.voltage_v, s_latest.current_a, s_latest.active_power_w);
                // 检查数据是否长时间不变
                if (memcmp(&s_latest, &last_sample, sizeof(power_sample_t)) == 0) {
                    same_count++;
                    if (same_count > SAME_LIMIT) {
                        ESP_LOGW(HLW8032_TAG, "检测到HLW8032数据%ds未变化，自动重启采集!", SAME_LIMIT);
                        // 重新初始化UART和采集任务
                        uart_driver_delete(HLW8032_UART_NUM);
                        dri_hlw8032_uart_init();
                        same_count = 0;
                    }
                } else {
                    same_count = 0;
                }
                last_sample = s_latest;
            }
            last_update_ticks = now;
        }
    }
}

/**
 * @brief 初始化HLW8032 UART接口
 */
void dri_hlw8032_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = HLW8032_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(HLW8032_UART_NUM, HLW8032_BUF_SIZE * 4, 512, 10, &hlw8032_uart_queue, 0);
    uart_param_config(HLW8032_UART_NUM, &uart_config);
    uart_set_pin(HLW8032_UART_NUM, HLW8032_TX_PIN, HLW8032_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    xTaskCreate(dri_hlw8032_uart_event_task, "hlw8032_uart", 4096, NULL, 12, NULL);
    ESP_LOGI(HLW8032_TAG, "HLW8032 UART初始化完成");
}

void dri_hlw8032_init(void)
{
    memset(&s_latest, 0, sizeof(s_latest));
    dri_hlw8032_uart_init();
    MY_LOGE("HLW8032芯片初始化完成");
}

power_sample_t power_meter_get_latest(void)
{
    return s_latest;
}