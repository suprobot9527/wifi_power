#include "int_protection.h"
#include "esp_log.h"

static const char *TAG = "protection";

/* 默认保护阈值 */
#define DEFAULT_OVERCURRENT_A 1.0f   /* 过流阈值 10A */
#define DEFAULT_OVERVOLTAGE_V 265.0f  /* 过压阈值 265V */
#define DEFAULT_UNDERVOLTAGE_V 175.0f /* 欠压阈值 175V */
#define DEFAULT_LEAKAGE_MA 30.0f      /* 漏电阈值 30mA */

/* 当前保护阈值（静态存储，可运行时修改） */
static protect_threshold_t s_threshold = {
    .overcurrent_a = DEFAULT_OVERCURRENT_A,
    .overvoltage_v = DEFAULT_OVERVOLTAGE_V,
    .undervoltage_v = DEFAULT_UNDERVOLTAGE_V,
    .leakage_ma = DEFAULT_LEAKAGE_MA,
};

/* 当前保护状态（静态存储，具有锁存记忆功能） */
static protect_state_t s_state = {
    .tripped = false,
    .reason = PROTECT_NONE,
};

/**
 * @brief 初始化保护模块
 * @note 将锁存状态强制置为“未触发”，确保上电后为正常状态
 */
void int_protection_init(void)
{
    s_state.tripped = false;
    s_state.reason = PROTECT_NONE;

    ESP_LOGI(TAG, "保护模块初始化完成: 过流%.1fA 过压%.0fV 欠压%.0fV 漏电%.0fmA",
             s_threshold.overcurrent_a, s_threshold.overvoltage_v,
             s_threshold.undervoltage_v, s_threshold.leakage_ma);
}

/**
 * @brief 动态设置保护阈值（立即生效）
 * @param th 指向新阈值配置结构体的指针
 */
void int_protection_set_threshold(const protect_threshold_t *th)
{
    s_threshold = *th;
    ESP_LOGI(TAG, "阈值更新: 过流%.1fA 过压%.0fV 欠压%.0fV 漏电%.0fmA",
             s_threshold.overcurrent_a, s_threshold.overvoltage_v,
             s_threshold.undervoltage_v, s_threshold.leakage_ma);
}

/**
 * @brief 获取当前生效的保护阈值配置
 * @return 保护阈值结构体副本
 */
protect_threshold_t int_protection_get_threshold(void)
{
    return s_threshold;
}

/**
 * @brief 根据采样数据执行保护检测（带锁存功能）
 * @param sample 当前电能采样数据指针
 * @return 保护状态（若已锁存故障则直接返回锁存值）
 * @note 一旦触发保护，即使后续参数恢复正常，状态仍保持为“已触发”，
 *       必须调用 int_protection_reset() 方可清除。
 */
protect_state_t int_protection_check(const power_sample_t *sample)
{
    /* 若已经触发保护，则保持状态不变（需手动复位） */
    if (s_state.tripped)
    {
        return s_state;
    }

    /* 采样数据无效时跳过检测，避免误触发 */
    if (!sample->valid)
    {
        return s_state;
    }

    /* 过流检测（优先级最高） */
    if (sample->current_a > s_threshold.overcurrent_a)
    {
        s_state.tripped = true;
        s_state.reason = PROTECT_OVERCURRENT;
        ESP_LOGW(TAG, "过流保护触发! 电流=%.2fA 阈值=%.1fA",
                 sample->current_a, s_threshold.overcurrent_a);
        return s_state;
    }

    /* 过压检测 */
    if (sample->voltage_v > s_threshold.overvoltage_v)
    {
        s_state.tripped = true;
        s_state.reason = PROTECT_OVERVOLTAGE;
        ESP_LOGW(TAG, "过压保护触发! 电压=%.1fV 阈值=%.0fV",
                 sample->voltage_v, s_threshold.overvoltage_v);
        return s_state;
    }

    /* 欠压检测 */
    if (sample->voltage_v < s_threshold.undervoltage_v)
    {
        s_state.tripped = true;
        s_state.reason = PROTECT_UNDERVOLTAGE;
        ESP_LOGW(TAG, "欠压保护触发! 电压=%.1fV 阈值=%.0fV",
                 sample->voltage_v, s_threshold.undervoltage_v);
        return s_state;
    }

    /* 漏电检测 */
    if (sample->leakage_ma > s_threshold.leakage_ma)
    {
        s_state.tripped = true;
        s_state.reason = PROTECT_LEAKAGE;
        ESP_LOGW(TAG, "漏电保护触发! 漏电流=%.1fmA 阈值=%.0fmA",
                 sample->leakage_ma, s_threshold.leakage_ma);
        return s_state;
    }

    /* 所有保护条件均未触发 */
    return s_state;
}

/**
 * @brief 复位保护状态（清除锁存故障）
 * @note 调用后保护状态恢复为“未触发”，可重新合闸
 */
void int_protection_reset(void)
{
    s_state.tripped = false;
    s_state.reason = PROTECT_NONE;
    ESP_LOGI(TAG, "保护状态已复位");
}

/**
 * @brief 获取当前保护状态（只读）
 * @return 保护状态结构体副本
 */
protect_state_t int_protection_get_state(void)
{
    return s_state;
}

/**
 * @brief 将保护原因枚举转换为中文可读字符串
 * @param reason 保护原因枚举值
 * @return 对应的中文描述字符串
 */
const char *int_protection_reason_str(protect_reason_t reason)
{
    switch (reason)
    {
    case PROTECT_NONE:
        return "正常";
    case PROTECT_OVERCURRENT:
        return "过流保护";
    case PROTECT_OVERVOLTAGE:
        return "过压保护";
    case PROTECT_UNDERVOLTAGE:
        return "欠压保护";
    case PROTECT_LEAKAGE:
        return "漏电保护";
    default:
        return "未知";
    }
}