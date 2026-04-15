#ifndef __APP_KEY_H__
#define __APP_KEY_H__

/**
 * @brief 初始化按键并启动按键处理任务
 *        按键1: 切换继电器开/关
 *        按键2: 强制关闭继电器
 *        按键3: 清零累计电能
 *        按键4: 预留
 */
void app_key_start(void);

#endif /* __APP_KEY_H__ */
