#include "key_ctrl.h" // 包含按键控制模块的头文件（声明了初始化、轮询函数等）

#include <stdlib.h> // 标准库（未直接使用，但可能被其他宏依赖）

#include "app_config.h"          // 应用配置（定义 ADC 通道、按键对应的 ADC 中间值、容差等）
#include "esp_timer.h"           // 高精度定时器（用于获取毫秒时间戳）
#include "hal/adc_types.h"       // ADC 类型定义（如 ADC 单位、衰减等）
#include "esp_adc/adc_oneshot.h" // ESP-IDF 单次 ADC 读取驱动


#define APP_KEY_ADC_CH              ADC_CHANNEL_6
#define APP_KEY_ADC_MAX             4095
#define APP_KEY1_MID                600
#define APP_KEY2_MID                1500
#define APP_KEY3_MID                2400
#define APP_KEY4_MID                3300
#define APP_KEY_TOLERANCE           300

static adc_oneshot_unit_handle_t s_adc; // ADC 单元句柄（用于后续操作）
static uint64_t s_last_ms = 0;          // 上一次有效按键的时间戳（毫秒），用于去抖动

// 初始化 ADC 按键检测
void key_ctrl_init(void)
{
    // 配置 ADC 单元（使用 ADC1）
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1, // 选择 ADC1
    };
    // 创建 ADC 单元，并将句柄存入 s_adc
    adc_oneshot_new_unit(&unit_cfg, &s_adc);

    // 配置 ADC 通道（衰减、分辨率）
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,    // 衰减 12dB，可测量 0~3.6V 范围
        .bitwidth = ADC_BITWIDTH_12, // 12 位分辨率（0~4095）
    };
    // 将上述配置应用到指定的 ADC 通道（APP_KEY_ADC_CH，在 app_config.h 中定义）
    adc_oneshot_config_channel(s_adc, APP_KEY_ADC_CH, &chan_cfg);
}

// 辅助函数：求绝对值（因为标准库 abs 可能涉及整型提升，这里简单实现）
static int abs_i(int x)
{
    return x >= 0 ? x : -x;
}

// 轮询按键状态，返回检测到的按键事件（若无按键或处于防抖期间则返回 KEY_NONE）
app_key_t key_ctrl_poll(void)
{
    int val = 0; // 存储 ADC 读取的原始值（0~4095）
    // 从 ADC 通道读取一个采样值
    adc_oneshot_read(s_adc, APP_KEY_ADC_CH, &val);

    // 获取当前时间（毫秒）
    const uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    // 去抖动：若距离上次有效按键不足 180ms，则忽略本次按键
    if (now_ms - s_last_ms < 180)
    {
        return KEY_NONE;
    }

    // 依次比较当前 ADC 值与各个按键的“中间值”，若差值在容差范围内则判定为该按键按下
    // 注意：按键按下时 ADC 值会接近预设的中间值（由电阻分压决定）

    if (abs_i(val - APP_KEY1_MID) < APP_KEY_TOLERANCE)
    {
        s_last_ms = now_ms;      // 记录本次按键时间，用于防抖
        return KEY_TOGGLE_RELAY; // 返回“切换继电器”事件
    }
    if (abs_i(val - APP_KEY2_MID) < APP_KEY_TOLERANCE)
    {
        s_last_ms = now_ms;
        return KEY_FORCE_OFF; // 返回“强制关闭”事件
    }
    if (abs_i(val - APP_KEY3_MID) < APP_KEY_TOLERANCE)
    {
        s_last_ms = now_ms;
        return KEY_CLEAR_ALARM; // 返回“清除告警”事件
    }
    if (abs_i(val - APP_KEY4_MID) < APP_KEY_TOLERANCE)
    {
        s_last_ms = now_ms;
        return KEY_RESERVED; // 返回“保留按键”事件（预留功能）
    }
    return KEY_NONE; // 无匹配按键
}