
#include "dri_key_ctrl.h" // 按键控制头文件，声明了按键事件类型和接口


static button_handle_t s_btn[4] = {NULL};           // 4个ADC按键句柄
static volatile app_key_t s_last_event = KEY_NONE;   // 记录最近一次事件

// 按键事件回调函数，每当有按键按下时被调用
// 按键事件回调函数，带事件类型参数，便于扩展
static void key_event_cb(void *arg, void *data)
{
	int key_idx = (int)data; // data才是按键编号（0~3）
	button_event_t event = iot_button_get_event(arg); // 获取实际事件类型
	// 只处理按下事件
	if (event == BUTTON_PRESS_DOWN) {
		switch (key_idx) {
			case 0: s_last_event = KEY_TOGGLE_RELAY; break;   // 按键1
			case 1: s_last_event = KEY_FORCE_OFF; break;      // 按键2
			case 2: s_last_event = KEY_CLEAR_ALARM; break;    // 按键3
			case 3: s_last_event = KEY_RESERVED; break;       // 按键4
			default: s_last_event = KEY_NONE; break;
		}
	}
}

// 初始化ADC按键
void key_ctrl_init(void)
{
	// 4个按键的电压区间
	const uint16_t key_mid[4] = {APP_KEY1_MID, APP_KEY2_MID, APP_KEY3_MID, APP_KEY4_MID};
	button_adc_config_t adc_cfg = {
		.unit_id = APP_KEY_ADC_UNIT,
		.adc_channel = APP_KEY_ADC_CH,
	};
	button_config_t btn_cfg = {0};

	for (int i = 0; i < 4; ++i) {
		adc_cfg.button_index = i;
		// 计算区间，避免边界重叠
		if (i == 0) {
			adc_cfg.min = (0 + key_mid[i]) / 2;
		} else {
			adc_cfg.min = (key_mid[i - 1] + key_mid[i]) / 2;
		}
		if (i == 3) {
			adc_cfg.max = key_mid[i] + APP_KEY_TOLERANCE; // 限制上限，避免空闲电压误触发
		} else {
			adc_cfg.max = (key_mid[i] + key_mid[i + 1]) / 2;
		}
		// 创建ADC按键
		iot_button_new_adc_device(&btn_cfg, &adc_cfg, &s_btn[i]);
		// 注册按下事件回调
		iot_button_register_cb(s_btn[i], BUTTON_PRESS_DOWN, NULL, key_event_cb, (void *)i);
	}
}

// 轮询获取最近一次按键事件，获取后自动清除
app_key_t key_ctrl_poll(void)
{
	app_key_t ret = s_last_event;   // 读取最近一次事件
	s_last_event = KEY_NONE;        // 读取后清除，防止重复上报
	return ret;                     // 返回事件类型
}





