#ifndef __INT_ECTRIC_ENERGY_H__
#define __INT_ECTRIC_ENERGY_H__

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"


// HLW8032 PF脉冲引脚（原理图：HLW_PF → 光耦 → GPIO3）
#define HLW_PF_GPIO GPIO_NUM_3

/**
 * @brief 初始化PF脉冲计数（GPIO中断方式）
 */
void int_energy_init(void);

/**
 * @brief 获取累计电能（单位：Wh）
 * @return 累计电能值
 */
float int_energy_get_wh(void);

/**
 * @brief 获取当前脉冲计数
 * @return 脉冲总数
 */
uint32_t int_energy_get_pulse_count(void);

/**
 * @brief 清零累计电能和脉冲计数
 */
void int_energy_reset(void);

#endif /* __INT_ECTRIC_ENERGY_H__ */
