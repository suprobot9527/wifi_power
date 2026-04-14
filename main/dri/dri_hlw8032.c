
#include "dri_hlw8032.h"


// 存储最新的电能采样数据（电压、电流、功率、有效标志、时间戳）
static power_sample_t s_latest = {0};

// // 保存最近一次接收到的24字节HLW8032数据
// static uint8_t hlw8032_last_frame[24] = {0};

// 日志TAG
static const char *HLW8032_TAG = "hlw8032_uart";
// UART事件队列句柄
static QueueHandle_t hlw8032_uart_queue;

/**
 * @brief 从3字节大端数据中提取24位无符号整数
 */
static uint32_t hlw8032_get_uint24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/**
 * @brief 校验HLW8032数据帧的校验和（byte23 = byte0~byte22之和的低8位）
 */
static bool hlw8032_check_sum(const uint8_t *buf)
{
	uint8_t sum = 0;
	for (int i = 0; i < 23; ++i)
	{
		sum += buf[i];
	}
	return (sum == buf[23]);
}

/**
 * @brief 从24字节数据帧中提取电压、电流和功率信息
 *
 * HLW8032 帧结构（24字节）:
 *   [0]     状态寄存器（bit5=电压参数溢出, bit4=电流参数溢出）
 *   [1]     校验（byte2+byte3+byte4 的低8位）
 *   [2-4]   电压参数 Vparam（3字节）
 *   [5-7]   电压寄存器 Vreg（3字节）
 *   [8]     校验（byte9+byte10+byte11 的低8位）
 *   [9-11]  电流参数 Iparam（3字节）
 *   [12-14] 电流寄存器 Ireg（3字节）
 *   [15]    校验（byte16+byte17+byte18 的低8位）
 *   [16-18] 功率参数 Pparam（3字节）
 *   [19-21] 功率寄存器 Preg（3字节）
 *   [22]    数据更新寄存器
 *   [23]    校验和（byte2~byte22 之和的低8位）
 *
 * 计算公式：
 *   电压 = Vparam / Vreg    （结合分压电阻换算）
 *   电流 = Iparam / Ireg    （结合采样电阻换算）
 *   功率 = Pparam / Preg    （结合分压和采样电阻换算）
 */
static bool dri_hlw8032_extract_data(const uint8_t *buf, power_sample_t *out)
{
	// 状态寄存器校验：0x55 表示芯片误差修正功能正常
	if (buf[0] != 0x55)
	{
		return false; // 芯片校准数据异常，丢弃此帧
	}

	// 校验和校验
	if (!hlw8032_check_sum(buf))
	{
		ESP_LOGW(HLW8032_TAG, "校验和错误");
		return false;
	}

	// 提取电压参数和寄存器（各3字节）
	const uint32_t v_param = hlw8032_get_uint24(&buf[2]);
	const uint32_t v_reg   = hlw8032_get_uint24(&buf[5]);
	// 提取电流参数和寄存器
	const uint32_t i_param = hlw8032_get_uint24(&buf[9]);
	const uint32_t i_reg   = hlw8032_get_uint24(&buf[12]);
	// 提取功率参数和寄存器
	const uint32_t p_param = hlw8032_get_uint24(&buf[16]);
	const uint32_t p_reg   = hlw8032_get_uint24(&buf[19]);

	// Byte22: 数据更新寄存器，bit4=电压更新, bit3=电流更新, bit2=功率更新
	const uint8_t update_reg = buf[22];

	// 计算实际值（仅在对应数据更新且寄存器非零时更新）
	if ((update_reg & 0x10) && v_reg != 0)
	{
		out->voltage_v = ((float)v_param / (float)v_reg) * APP_CALIB_VOLTAGE_GAIN;
	}
	if ((update_reg & 0x08) && i_reg != 0)
	{
		out->current_a = ((float)i_param / (float)i_reg) * APP_CALIB_CURRENT_GAIN;
	}
	if ((update_reg & 0x04) && p_reg != 0)
	{
		out->active_power_w = ((float)p_param / (float)p_reg) * APP_CALIB_POWER_GAIN;
	}

	// 标记数据有效
	out->valid = true;
	// 记录采样时间戳（毫秒）
	out->timestamp_ms = esp_timer_get_time() / 1000ULL;
	return true;
}

/**
 * @brief HLW8032 UART事件处理任务
 *        负责接收UART事件并处理数据
 * @param pvParameters 任务参数（未使用）
 */
static void dri_hlw8032_uart_event_task(void *pvParameters)
{
	uart_event_t event;
	uint8_t *data = (uint8_t *)malloc(HLW8032_BUF_SIZE); // 分配数据缓冲区
	for (;;)
	{
		// 等待UART事件
		if (xQueueReceive(hlw8032_uart_queue, (void *)&event, (TickType_t)portMAX_DELAY))
		{
			switch (event.type)
			{
			case UART_DATA:
				// 收到数据事件，尝试读取24字节数据帧
				if (event.size >= 24)
				{
					int len = uart_read_bytes(HLW8032_UART_NUM, data, 24, portMAX_DELAY);
					if (len == 24)
					{
						// 打印24字节数据内容
						char hexstr[3 * 24 + 1] = {0};
						for (int i = 0; i < 24; ++i)
						{
							sprintf(hexstr + i * 3, "%02X ", data[i]);
						}
						ESP_LOGI(HLW8032_TAG, "收到HLW8032数据帧: %s", hexstr);

						power_sample_t sample = s_latest; // 复制当前最新采样数据
						// 尝试解析这一帧，若解析成功则更新s_latest
						if (dri_hlw8032_extract_data(data, &sample))
						{
							// 将PF脉冲计算的累计电能写入结构体
							sample.energy_wh_total = int_energy_get_wh();
							s_latest = sample;
						}
					}
					else
					{
						ESP_LOGW(HLW8032_TAG, "读取数据长度异常: %d", len);
					}
				}
				else
				{
					// 丢弃非完整帧
					uart_flush_input(HLW8032_UART_NUM);
					ESP_LOGW(HLW8032_TAG, "接收字节数不足24，已丢弃");
				}
				break;
			case UART_FIFO_OVF:
			case UART_BUFFER_FULL:
				// FIFO溢出或缓冲区满，清空输入缓冲区并重置队列
				uart_flush_input(HLW8032_UART_NUM);
				xQueueReset(hlw8032_uart_queue);
				ESP_LOGW(HLW8032_TAG, "UART溢出或缓冲区满");
				break;
			default:
				// 其他UART事件
				ESP_LOGI(HLW8032_TAG, "UART事件类型: %d", event.type);
				break;
			}
		}
	}
	free(data);
	vTaskDelete(NULL);
}

/**
 * @brief 初始化HLW8032 UART接口
 *        配置UART参数、引脚并创建事件处理任务
 */
void dri_hlw8032_uart_init(void)
{
	// 配置UART参数
	uart_config_t uart_config = {
		.baud_rate = HLW8032_BAUD_RATE,		   // 波特率
		.data_bits = UART_DATA_8_BITS,		   // 8位数据位
		.parity = UART_PARITY_EVEN,		   // 有校验
		.stop_bits = UART_STOP_BITS_1,		   // 1位停止位
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE, // 无硬件流控
		.source_clk = UART_SCLK_DEFAULT,	   // 默认时钟
	};
	// 安装UART驱动，分配缓冲区和事件队列
	uart_driver_install(HLW8032_UART_NUM, HLW8032_BUF_SIZE * 2, 0, 10, &hlw8032_uart_queue, 0);
	// 设置UART参数
	uart_param_config(HLW8032_UART_NUM, &uart_config);
	// 设置UART引脚
	// 只设置RX引脚，TX引脚不设置
	uart_set_pin(HLW8032_UART_NUM, HLW8032_TX_PIN, HLW8032_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	// 创建UART事件处理任务
	xTaskCreate(dri_hlw8032_uart_event_task, "hlw8032_uart_task", 2048, NULL, 10, NULL);
	ESP_LOGI(HLW8032_TAG, "HLW8032 UART初始化完成");
}

/**
 * @brief 初始化HLW8032芯片
 *        目前仅清空电能数据结构并初始化UART接口
 */
void dri_hlw8032_init(void)
{
	// 清空电能结构体
	memset(&s_latest, 0, sizeof(s_latest));
	// 初始化UART接口
	dri_hlw8032_uart_init();
	MY_LOGE("HLW8032芯片初始化完成");
}

/*
 * @brief 获取最新的电能采样数据
 * @return 最新的电能采样数据
 */
power_sample_t power_meter_get_latest(void)
{

	// 返回最新的采样数据（无论是否有新帧，都返回上次成功解析的数据）
	return s_latest;
}



