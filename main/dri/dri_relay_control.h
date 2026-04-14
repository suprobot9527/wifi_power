#ifndef __DRI_RELAY_CONTROL_H__
#define __DRI_RELAY_CONTROL_H__


#include "driver/gpio.h"        // ESP-IDF GPIO驱动头文件

#define RELAY_GPIO 10           // 继电器连接的GPIO编号
// 初始化继电器GPIO，默认关闭
void dri_relay_init(void);
// 控制继电器开关
void dri_relay_set(bool on);
// 获取继电器当前状态
bool dri_relay_get_state(void);

#endif /* __DRI_RELAY_CONTROL_H__ */

