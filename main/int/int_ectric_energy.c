#include "int_ectric_energy.h" // 包含自定义的电能计量模块头文件

static const char *TAG = "energy_pf"; // 定义日志标签，用于ESP_LOG输出时标识模块名称

// HLW8032 每kWh对应的PF脉冲数（需根据芯片参数和采样电阻校准，默认值3200）
// 实际值 = 功率参数寄存器值 / (电压系数 * 电流系数 * 采样电阻)
// 请根据实际硬件校准修改
#define HLW_PF_PULSES_PER_KWH 3200 // 宏定义：每消耗1度电（kWh）产生的脉冲个数

// 脉冲计数（volatile，ISR中修改）
static volatile uint32_t s_pulse_count = 0; // 静态全局变量，用于累计脉冲数，使用volatile防止编译器优化，确保在中断与主循环间正确读写

/**
 * @brief PF引脚下降沿中断服务函数
 *        光耦输出为低电平有效，每个脉冲产生一个下降沿
 */
static void IRAM_ATTR pf_isr_handler(void *arg) // 中断服务函数，IRAM_ATTR属性使函数运行在内部RAM中，提高中断响应速度
{
    s_pulse_count++; // 每来一个下降沿中断，脉冲计数加1
}

/**
 * @brief 初始化PF脉冲计数
 *        配置GPIO3为下降沿中断，注册中断服务
 */
void int_energy_init(void) // 电能计量模块初始化函数
{
    gpio_config_t io_conf = {
        // 定义GPIO配置结构体变量
        .pin_bit_mask = (1ULL << HLW_PF_GPIO), // 指定要配置的引脚位掩码（HLW_PF_GPIO为头文件中定义的GPIO号）
        .mode = GPIO_MODE_INPUT,               // 设置为输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,      // 使能内部上拉电阻（光耦输出空闲时为高电平）
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 禁止内部下拉电阻
        .intr_type = GPIO_INTR_NEGEDGE,        // 中断触发类型：下降沿触发（光耦低电平有效）
    };
    gpio_config(&io_conf); // 调用ESP-IDF的GPIO配置函数，按上述参数初始化引脚

    // 安装GPIO中断服务（如果已安装则忽略错误）
    gpio_install_isr_service(0); // 参数0表示使用默认的ESP_INTR_FLAG_DEFAULT，为GPIO全局中断服务分配资源
    // 注册PF引脚中断处理函数
    gpio_isr_handler_add(HLW_PF_GPIO, pf_isr_handler, NULL); // 将指定引脚的中断服务函数添加到中断向量表

    s_pulse_count = 0;                                           // 初始化脉冲计数为0
    ESP_LOGI(TAG, "PF脉冲计数初始化完成, GPIO=%d", HLW_PF_GPIO); // 打印日志，表明初始化成功并输出使用的GPIO号
}

/**
 * @brief 获取累计电能（单位：Wh）
 */
float int_energy_get_wh(void) // 返回累计电能值，单位为瓦时（Wh）
{
    uint32_t count = s_pulse_count; // 读取当前脉冲计数值（原子操作取决于具体CPU架构，32位CPU对uint32_t的读取一般是原子的，但为避免竞态可加临界区保护）
    // 1 kWh = 1000 Wh，所以 Wh = count / (pulses_per_kWh) * 1000
    return (float)count / (float)HLW_PF_PULSES_PER_KWH * 1000.0f; // 脉冲数除以每度电脉冲数得到度数（kWh），再乘以1000转换为瓦时（Wh）
}

/**
 * @brief 获取当前脉冲计数
 */
uint32_t int_energy_get_pulse_count(void) // 返回当前累计的原始脉冲数
{
    return s_pulse_count; // 直接返回脉冲计数变量
}

/**
 * @brief 清零累计电能和脉冲计数
 */
void int_energy_reset(void) // 清零函数，将累计电能归零
{
    portDISABLE_INTERRUPTS();        // 关中断（临界区保护），防止在清零过程中发生中断修改s_pulse_count
    s_pulse_count = 0;               // 将脉冲计数置零
    portENABLE_INTERRUPTS();         // 开中断
    ESP_LOGI(TAG, "累计电能已清零"); // 输出日志提示清零完成
}