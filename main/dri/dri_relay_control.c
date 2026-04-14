
#include "dri_relay_control.h" // 继电器控制头文件


static bool relay_state = false; // 继电器当前状态，false: 关闭，true: 打开

// 初始化继电器GPIO，默认关闭
void dri_relay_init(void)
{
	// 配置GPIO参数结构体
	gpio_config_t io_conf = {
		.pin_bit_mask = (1ULL << RELAY_GPIO), // 选择GPIO10
		.mode = GPIO_MODE_OUTPUT,             // 设置为输出模式
		.pull_up_en = GPIO_PULLUP_DISABLE,    // 禁用上拉
		.pull_down_en = GPIO_PULLDOWN_DISABLE,// 禁用下拉
		.intr_type = GPIO_INTR_DISABLE        // 禁用中断
	};
	gpio_config(&io_conf);                   // 初始化GPIO配置
	gpio_set_level(RELAY_GPIO, 0);           // 默认输出低电平，关闭继电器
	relay_state = false;                     // 状态变量设为关闭
}

// 控制继电器开关
void dri_relay_set(bool on)
{
	gpio_set_level(RELAY_GPIO, on ? 1 : 0);  // 输出高电平打开，低电平关闭
	relay_state = on;                        // 更新状态变量
}

// 获取继电器当前状态
bool dri_relay_get_state(void)
{
	return relay_state;                      // 返回当前状态
}

