#include "dri_key_ctrl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "soc/soc_caps.h"

static const char *TAG = "key_ctrl";

static button_handle_t s_btn[4] = {NULL};
static volatile app_key_t s_last_event = KEY_NONE;
static volatile int64_t s_last_event_time = 0;

static adc_oneshot_unit_handle_t s_key_adc_handle = NULL;
static adc_cali_handle_t s_key_adc_cali_handle = NULL;
static bool s_key_adc_calibrated = false;
static uint16_t s_key_min_mv[4] = {0};
static uint16_t s_key_max_mv[4] = {0};

#define KEY_DEBOUNCE_MS 300
#define KEY_ADC_ATTEN (ADC_ATTEN_DB_6 + 1)

typedef struct
{
    int raw;
    int mv;
    bool ok;
} key_adc_sample_t;

static bool key_adc_cali_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    bool calibrated = false;
    adc_cali_handle_t handle = NULL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_cfg = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = SOC_ADC_RTC_MAX_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&curve_cfg, &handle) == ESP_OK)
    {
        calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        adc_cali_line_fitting_config_t line_cfg = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = SOC_ADC_RTC_MAX_BITWIDTH,
        };
        if (adc_cali_create_scheme_line_fitting(&line_cfg, &handle) == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = calibrated ? handle : NULL;
    if (!calibrated)
    {
        ESP_LOGW(TAG, "ADC calibration unavailable, will print raw value only");
    }
    return calibrated;
}

static esp_err_t key_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = APP_KEY_ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_key_adc_handle), TAG, "adc unit init failed");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = SOC_ADC_RTC_MAX_BITWIDTH,
        .atten = KEY_ADC_ATTEN,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_key_adc_handle, APP_KEY_ADC_CH, &chan_cfg), TAG, "adc channel config failed");

    s_key_adc_calibrated = key_adc_cali_init(APP_KEY_ADC_UNIT, KEY_ADC_ATTEN, &s_key_adc_cali_handle);
    return ESP_OK;
}

static key_adc_sample_t key_adc_sample(void)
{
    key_adc_sample_t sample = {
        .raw = 0,
        .mv = -1,
        .ok = false,
    };

    if (s_key_adc_handle == NULL)
    {
        return sample;
    }

    if (adc_oneshot_read(s_key_adc_handle, APP_KEY_ADC_CH, &sample.raw) != ESP_OK)
    {
        return sample;
    }

    sample.ok = true;
    if (s_key_adc_calibrated && s_key_adc_cali_handle)
    {
        if (adc_cali_raw_to_voltage(s_key_adc_cali_handle, sample.raw, &sample.mv) != ESP_OK)
        {
            sample.mv = -1;
        }
    }

    return sample;
}

static void key_event_cb(void *arg, void *data)
{
    int key_idx = (int)(intptr_t)data;
    button_event_t event = iot_button_get_event(arg);
    if (event != BUTTON_PRESS_DOWN)
    {
        return;
    }

    int64_t now = esp_timer_get_time();
    if ((now - s_last_event_time) < (KEY_DEBOUNCE_MS * 1000))
    {
        ESP_LOGD(TAG, "debounce filtered: key_idx=%d, gap=%lldms", key_idx, (now - s_last_event_time) / 1000);
        return;
    }

    key_adc_sample_t adc = key_adc_sample();
    if (key_idx >= 0 && key_idx < 4)
    {
        if (adc.ok)
        {
            if (adc.mv >= 0)
            {
                ESP_LOGI(TAG, "key=%d, adc=%dmV(raw=%d), range=[%u,%u]mV",
                         key_idx, adc.mv, adc.raw, s_key_min_mv[key_idx], s_key_max_mv[key_idx]);
            }
            else
            {
                ESP_LOGI(TAG, "key=%d, adc_raw=%d, range=[%u,%u]mV",
                         key_idx, adc.raw, s_key_min_mv[key_idx], s_key_max_mv[key_idx]);
            }
        }
        else
        {
            ESP_LOGI(TAG, "key=%d, adc read failed", key_idx);
        }
    }

    s_last_event_time = now;
    switch (key_idx)
    {
    case 0:
        s_last_event = KEY_TOGGLE_RELAY;
        break;
    case 1:
        s_last_event = KEY_FORCE_OFF;
        break;
    case 2:
        s_last_event = KEY_CLEAR_ALARM;
        break;
    case 3:
        s_last_event = KEY_RESERVED;
        break;
    default:
        s_last_event = KEY_NONE;
        break;
    }
}

void key_ctrl_init(void)
{
    // 手动设置每个按键的ADC区间，区间之间至少留100冗余
    // 按键1: [0, 1100)
    // 按键2: [1200, 2850)
    // 按键3: [2950, 3240)
    // 按键4: [3340, 3499]
    const uint16_t key_min[4] = {0, 1000, 1900, 2400};
    const uint16_t key_max[4] = {500, 1800, 2300, 2700};
    ESP_ERROR_CHECK(key_adc_init());

    button_adc_config_t adc_cfg = {
        .adc_handle = &s_key_adc_handle,
        .unit_id = APP_KEY_ADC_UNIT,
        .adc_channel = APP_KEY_ADC_CH,
    };
    button_config_t btn_cfg = {0};

    for (int i = 0; i < 4; ++i)
    {
        adc_cfg.button_index = i;
        adc_cfg.min = key_min[i];
        adc_cfg.max = key_max[i];
        s_key_min_mv[i] = adc_cfg.min;
        s_key_max_mv[i] = adc_cfg.max;
        ESP_LOGI(TAG, "key%d range [%d, %d] mV", i + 1, adc_cfg.min, adc_cfg.max);
        iot_button_new_adc_device(&btn_cfg, &adc_cfg, &s_btn[i]);
        iot_button_register_cb(s_btn[i], BUTTON_PRESS_DOWN, NULL, key_event_cb, (void *)(intptr_t)i);
    }
}

app_key_t key_ctrl_poll(void)
{
    app_key_t ret = s_last_event;
    s_last_event = KEY_NONE;
    return ret;
}
