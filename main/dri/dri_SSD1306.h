#ifndef __DRI_SSD1306_H__
#define __DRI_SSD1306_H__

#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "ssd1306_fonts.h"

// I2C 引脚定义
#define OLED_I2C_SDA_PIN        8
#define OLED_I2C_SCL_PIN        9
#define OLED_I2C_FREQ_HZ        400000

// SSD1306 参数
#define SSD1306_I2C_ADDR        0x3C
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64

/**
 * @brief 初始化I2C总线和SSD1306 OLED屏幕 (使用新版i2c_master API)
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t dri_oled_init(void);

/**
 * @brief 在OLED上显示字符串
 * @param x X坐标
 * @param y Y坐标
 * @param str 字符串
 * @param size 字体大小 (12 或 16)
 */
void dri_oled_show_string(uint8_t x, uint8_t y, const char *str, uint8_t size);

/**
 * @brief 在OLED上显示数字
 * @param x X坐标
 * @param y Y坐标
 * @param num 数字
 * @param len 显示长度
 * @param size 字体大小
 */
void dri_oled_show_num(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size);

/**
 * @brief 清屏
 */
void dri_oled_clear(void);

/**
 * @brief 清空显示缓冲区（不刷新屏幕）
 */
void dri_oled_clear_buf(void);

/**
 * @brief 写字符串到缓冲区（不刷新屏幕）
 */
void dri_oled_show_string_buf(uint8_t x, uint8_t y, const char *str, uint8_t size);

/**
 * @brief 刷新显示 (将GRAM写入屏幕)
 * @return ESP_OK 成功
 */
esp_err_t dri_oled_refresh(void);

/**
 * @brief 在指定位置画点
 */
void dri_oled_fill_point(uint8_t x, uint8_t y, uint8_t point);

/**
 * @brief 画线
 */
void dri_oled_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2);

/**
 * @brief 填充矩形
 */
void dri_oled_fill_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t dot);

/**
 * @brief 显示位图
 */
void dri_oled_draw_bitmap(uint8_t x, uint8_t y, const uint8_t *bmp, uint8_t w, uint8_t h);

#endif /* __DRI_SSD1306_H__ */
