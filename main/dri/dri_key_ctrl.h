#ifndef __DRI_KEY_CTRL_H__
#define __DRI_KEY_CTRL_H__


#include <stdio.h>
#include "iot_button.h"    // 官方button驱动头文件
#include "button_adc.h"   // ADC按键扩展头文件

#define APP_KEY_ADC_CH              ADC_CHANNEL_0   // ADC通道（GPIO0 = ADC1_CH0）
#define APP_KEY_ADC_UNIT            ADC_UNIT_1      // ADC单元
#define APP_KEY1_MID                600             // 按键1中值
#define APP_KEY2_MID                1500            // 按键2中值
#define APP_KEY3_MID                2400            // 按键3中值
#define APP_KEY4_MID                3300            // 按键4中值
#define APP_KEY_TOLERANCE           300             // 容差

typedef enum {
    KEY_NONE = 0,       // 无按键 / 空闲状态
    KEY_TOGGLE_RELAY,   // 切换继电器状态（开/关）
    KEY_FORCE_OFF,      // 强制断开继电器（急停/优先断开）
    KEY_CLEAR_ALARM,    // 清除报警/故障指示
    KEY_RESERVED,       // 保留按键（预留功能，暂未使用）
} app_key_t;

/**
 * @brief 初始化ADC按键
 */
void key_ctrl_init(void);

/**
 * @brief 轮询获取最近一次按键事件，获取后自动清除
 * @return 按键事件类型
 */
app_key_t key_ctrl_poll(void);

#endif /* __DRI_KEY_CTRL_H__ */

