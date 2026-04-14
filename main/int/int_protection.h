#ifndef __INT_PROTECTION_H__
#define __INT_PROTECTION_H__

#include <stdbool.h>
#include "com_power_types.h"
#include "esp_log.h"
typedef enum
{
    // 定义枚举类型，用于标识保护触发的原因
    PROTECT_NONE = 0,     // 无保护触发，正常状态
    PROTECT_OVERCURRENT,  // 过流保护（电流超过设定阈值）
    PROTECT_OVERVOLTAGE,  // 过压保护（电压超过设定阈值）
    PROTECT_UNDERVOLTAGE, // 欠压保护（电压低于设定阈值）
    PROTECT_LEAKAGE,      // 漏电保护（漏电流超过设定阈值）
} protect_reason_t;
// protect_reason_t 枚举类型，列出了所有可能的保护原因

typedef struct
{
    // 定义结构体，用于描述当前保护状态
    bool tripped;            // 布尔标志：true 表示已触发保护，false 表示未触发
    protect_reason_t reason; // 若触发保护，则记录具体原因；否则为 PROTECT_NONE
} protect_state_t;

// 保护阈值配置结构体
typedef struct
{
    float overcurrent_a;    // 过流阈值（A）
    float overvoltage_v;    // 过压阈值（V）
    float undervoltage_v;   // 欠压阈值（V）
    float leakage_ma;       // 漏电阈值（mA）
} protect_threshold_t;

/**
 * @brief 初始化保护模块（使用默认阈值）
 */
void int_protection_init(void);

/**
 * @brief 设置保护阈值
 */
void int_protection_set_threshold(const protect_threshold_t *th);

/**
 * @brief 获取当前保护阈值
 */
protect_threshold_t int_protection_get_threshold(void);

/**
 * @brief 根据采样数据检测是否需要触发保护
 * @param sample 当前电能采样数据
 * @return 保护状态
 */
protect_state_t int_protection_check(const power_sample_t *sample);

/**
 * @brief 复位保护状态（清除告警）
 */
void int_protection_reset(void);

/**
 * @brief 获取当前保护状态
 */
protect_state_t int_protection_get_state(void);

/**
 * @brief 将保护原因枚举转换为可读字符串
 */
const char *int_protection_reason_str(protect_reason_t reason);

#endif /* __INT_PROTECTION_H__ */