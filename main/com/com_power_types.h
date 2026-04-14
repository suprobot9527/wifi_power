#ifndef __COM_POWER_TYPES_H__
#define __COM_POWER_TYPES_H__

#include <stdbool.h> // 提供 bool 类型（true/false）
#include <stdint.h>  // 提供 uint64_t 等固定宽度整数类型

/**
 * @brief 电能采样数据结构体
 *
 * 该结构体用于存储从 HLW8032 芯片解析出的电气参数以及附加状态信息。
 * 在 power_meter_hlw8032.c 中，全局变量 s_latest 就是该类型，
 * 函数 power_meter_get_latest() 返回的也是该结构体的副本。
 */
typedef struct
{
    float voltage_v;       // 电压（单位：伏特 V），对应 parse_frame_24() 中的 out->voltage_v
    float current_a;       // 电流（单位：安培 A），对应 parse_frame_24() 中的 out->current_a
    float active_power_w;  // 有功功率（单位：瓦特 W），对应 parse_frame_24() 中的 out->active_power_w
    float energy_wh_total; // 累计电能（单位：瓦时 Wh），当前驱动未使用，保留为扩展字段
    float leakage_ma;      // 漏电流（单位：毫安 mA），当前驱动未使用，保留为扩展字段
    bool valid;            // 数据有效性标志，true 表示本次解析成功，由 parse_frame_24() 设置
    uint64_t timestamp_ms; // 采样时间戳（毫秒），由 parse_frame_24() 通过 esp_timer_get_time() 获取
} power_sample_t;

#endif /* __COM_POWER_TYPES_H__ */
