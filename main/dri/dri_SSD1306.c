#include "dri_SSD1306.h"

static const char *TAG = "DRI_SSD1306";

// I2C 设备句柄
static i2c_master_dev_handle_t s_i2c_dev = NULL;

// 显示缓冲区 [128][8]
static uint8_t s_display_buf[SSD1306_WIDTH][8];

// ======================== I2C 底层通信 ========================

static esp_err_t oled_write_cmd(const uint8_t *data, uint16_t len)
{
    // 命令格式: [0x00(control byte), cmd1, cmd2, ...]
    uint8_t buf[len + 1];
    buf[0] = 0x00; // Co=0, D/C#=0 -> command
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_i2c_dev, buf, len + 1, 1000);
}

static esp_err_t oled_write_cmd_byte(uint8_t cmd)
{
    return oled_write_cmd(&cmd, 1);
}

static esp_err_t oled_write_data(const uint8_t *data, uint16_t len)
{
    // 数据格式: [0x40(control byte), data1, data2, ...]
    uint8_t buf[len + 1];
    buf[0] = 0x40; // Co=0, D/C#=1 -> data
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_i2c_dev, buf, len + 1, 1000);
}

// ======================== 显示缓冲操作 ========================

void dri_oled_fill_point(uint8_t x, uint8_t y, uint8_t point)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;
    }
    uint8_t pos = 7 - y / 8;
    uint8_t bx = y % 8;
    uint8_t temp = 1 << (7 - bx);

    if (point) {
        s_display_buf[x][pos] |= temp;
    } else {
        s_display_buf[x][pos] &= ~temp;
    }
}

void dri_oled_fill_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t dot)
{
    for (uint8_t x = x1; x <= x2; x++) {
        for (uint8_t y = y1; y <= y2; y++) {
            dri_oled_fill_point(x, y, dot);
        }
    }
}

void dri_oled_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    int16_t x_len = abs(x1 - x2);
    int16_t y_len = abs(y1 - y2);

    if (y_len < x_len) {
        if (x1 > x2) {
            int16_t t = x1; x1 = x2; x2 = t;
            t = y1; y1 = y2; y2 = t;
        }
        int16_t len = x_len, diff = y_len;
        do {
            if (diff >= x_len) {
                diff -= x_len;
                y1 += (y1 < y2) ? 1 : -1;
            }
            diff += y_len;
            dri_oled_fill_point(x1++, y1, 1);
        } while (len--);
    } else {
        if (y1 > y2) {
            int16_t t = x1; x1 = x2; x2 = t;
            t = y1; y1 = y2; y2 = t;
        }
        int16_t len = y_len, diff = x_len;
        do {
            if (diff >= y_len) {
                diff -= y_len;
                x1 += (x1 < x2) ? 1 : -1;
            }
            diff += x_len;
            dri_oled_fill_point(x1, y1++, 1);
        } while (len--);
    }
}

// ======================== 字符绘制 ========================

static void oled_draw_char(uint8_t x, uint8_t y, uint8_t chr, uint8_t size, uint8_t mode)
{
    uint8_t i, j;
    uint8_t temp, y0 = y;

    chr = chr - ' ';
    for (i = 0; i < size; i++) {
        if (size == 12) {
            temp = mode ? c_chFont1206[chr][i] : ~c_chFont1206[chr][i];
        } else {
            temp = mode ? c_chFont1608[chr][i] : ~c_chFont1608[chr][i];
        }
        for (j = 0; j < 8; j++) {
            dri_oled_fill_point(x, y, (temp & 0x80) ? 1 : 0);
            temp <<= 1;
            y++;
            if ((y - y0) == size) {
                y = y0;
                x++;
                break;
            }
        }
    }
}

static uint32_t oled_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--) result *= m;
    return result;
}

void dri_oled_show_string(uint8_t x, uint8_t y, const char *str, uint8_t size)
{
    if (str == NULL) return;
    while (*str != '\0') {
        if (x > (SSD1306_WIDTH - size / 2)) {
            x = 0;
            y += size;
            if (y > (SSD1306_HEIGHT - size)) {
                y = x = 0;
                dri_oled_clear();
            }
        }
        oled_draw_char(x, y, *str, size, 1);
        x += size / 2;
        str++;
    }
    dri_oled_refresh();
}

void dri_oled_show_num(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size)
{
    uint8_t show = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t temp = (num / oled_pow(10, len - i - 1)) % 10;
        if (show == 0 && i < (len - 1)) {
            if (temp == 0) {
                oled_draw_char(x + (size / 2) * i, y, ' ', size, 1);
                continue;
            } else {
                show = 1;
            }
        }
        oled_draw_char(x + (size / 2) * i, y, temp + '0', size, 1);
    }
    dri_oled_refresh();
}

void dri_oled_draw_bitmap(uint8_t x, uint8_t y, const uint8_t *bmp, uint8_t w, uint8_t h)
{
    uint16_t byteWidth = (w + 7) / 8;
    for (uint16_t j = 0; j < h; j++) {
        for (uint16_t i = 0; i < w; i++) {
            if (*(bmp + j * byteWidth + i / 8) & (128 >> (i & 7))) {
                dri_oled_fill_point(x + i, y + j, 1);
            }
        }
    }
}

// ======================== 屏幕控制 ========================

void dri_oled_clear(void)
{
    memset(s_display_buf, 0x00, sizeof(s_display_buf));
    dri_oled_refresh();
}

void dri_oled_clear_buf(void)
{
    memset(s_display_buf, 0x00, sizeof(s_display_buf));
}

void dri_oled_show_string_buf(uint8_t x, uint8_t y, const char *str, uint8_t size)
{
    if (str == NULL) return;
    while (*str != '\0') {
        if (x > (SSD1306_WIDTH - size / 2)) {
            x = 0;
            y += size;
            if (y > (SSD1306_HEIGHT - size)) return;
        }
        oled_draw_char(x, y, *str, size, 1);
        x += size / 2;
        str++;
    }
}

esp_err_t dri_oled_refresh(void)
{
    if (s_i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return oled_write_data(&s_display_buf[0][0], sizeof(s_display_buf));
}

// ======================== 初始化 ========================

esp_err_t dri_oled_init(void)
{
    esp_err_t ret;

    // 1. 初始化I2C总线 (新版 i2c_master API)
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_I2C_SDA_PIN,
        .scl_io_num = OLED_I2C_SCL_PIN,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 添加SSD1306设备
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &s_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C master initialized: SDA=%d, SCL=%d", OLED_I2C_SDA_PIN, OLED_I2C_SCL_PIN);

    // 3. SSD1306初始化命令序列 (参考官方ssd1306.c)
    oled_write_cmd_byte(0xAE); // 关闭显示
    oled_write_cmd_byte(0x40); // 设置起始行地址
    oled_write_cmd_byte(0x81); // 设置对比度
    oled_write_cmd_byte(0xCF); // 对比度值
    oled_write_cmd_byte(0xA1); // 段重映射
    oled_write_cmd_byte(0xC0); // COM扫描方向
    oled_write_cmd_byte(0xA6); // 正常显示
    oled_write_cmd_byte(0xA8); // 多路复用率
    oled_write_cmd_byte(0x3F); // 1/64 duty
    oled_write_cmd_byte(0xD5); // 显示时钟分频
    oled_write_cmd_byte(0x80); // 分频值
    oled_write_cmd_byte(0xD9); // 预充电周期
    oled_write_cmd_byte(0xF1); // 预充电值
    oled_write_cmd_byte(0xDA); // COM引脚配置
    oled_write_cmd_byte(0xDB); // VCOMH电压
    oled_write_cmd_byte(0x40); // VCOMH值
    oled_write_cmd_byte(0x8D); // 电荷泵
    oled_write_cmd_byte(0x14); // 开启电荷泵
    oled_write_cmd_byte(0xA4); // 全屏显示关闭
    oled_write_cmd_byte(0xA6); // 正常显示模式

    // 设置垂直寻址模式
    const uint8_t cmd_addr_mode[2] = {0x20, 0x01};
    oled_write_cmd(cmd_addr_mode, sizeof(cmd_addr_mode));

    // 设置列地址范围
    uint8_t cmd_col[3] = {0x21, 0, 127};
    oled_write_cmd(cmd_col, sizeof(cmd_col));

    // 设置页地址范围
    uint8_t cmd_page[3] = {0x22, 0, 7};
    oled_write_cmd(cmd_page, sizeof(cmd_page));

    // 开启显示
    ret = oled_write_cmd_byte(0xAF);

    // 4. 清屏
    memset(s_display_buf, 0x00, sizeof(s_display_buf));
    dri_oled_refresh();

    ESP_LOGI(TAG, "OLED initialized successfully");
    return ret;
}