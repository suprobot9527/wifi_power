#ifndef _DRI_HLW8032_H_
#define _DRI_HLW8032_H_
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "com_power_types.h" // 引入电能采样数据结构定义
#include "com_debug.h"
#include "esp_timer.h"
#include "int_ectric_energy.h"

// HLW8032所用UART端口号（主控U0RXD）
#define HLW8032_UART_NUM UART_NUM_0
// HLW8032通信波特率
#define HLW8032_BAUD_RATE 4800
// UART缓冲区大小
#define HLW8032_BUF_SIZE 256
// HLW8032 RX引脚（U0RXD = GPIO20）
#define HLW8032_RX_PIN 20
// HLW8032 TX引脚（未连接，不设置）
#define HLW8032_TX_PIN UART_PIN_NO_CHANGE

// -------- HLW8032 calibration --------
#define APP_CALIB_VOLTAGE_GAIN   1.868f   // 电压校准系数
#define APP_CALIB_CURRENT_GAIN   1.0f     // 电流校准系数（待校准）
#define APP_CALIB_POWER_GAIN     1.0f     // 功率校准系数（待校准）

/**
 * @brief 初始化HLW8032 UART接口
 */
void dri_hlw8032_uart_init(void);

/**
 * @brief 初始化HLW8032芯片
 */
void dri_hlw8032_init(void);

/*
 * @brief 获取最新的电能采样数据
 * @return 最新的电能采样数据
 */
power_sample_t power_meter_get_latest(void);

#endif // _DRI_HLW8032_H_
