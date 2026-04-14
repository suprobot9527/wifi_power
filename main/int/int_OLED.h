#ifndef __INT_OLED_H__
#define __INT_OLED_H__

#include "esp_err.h"
#include "dri_SSD1306.h"
#include "com_power_types.h"
#include "dri_hlw8032.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
/**
 * @brief 初始化OLED并启动显示刷新任务
 * @return ESP_OK 成功
 */
esp_err_t int_oled_init(void);

#endif /* __INT_OLED_H__ */
