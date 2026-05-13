// battery.cpp
#include "battery.h"
#include <algorithm>

// GPIO52 = ADC1_CHANNEL_3 on ESP32-P4
#define BAT_ADC_CHANNEL ADC_CHANNEL_3

// Calibration from community measurements on this board
// GPIO52 reads ~2430mV at full (4.2V battery)
// GPIO52 reads ~1800mV at low  (~3.3V battery)
// Scale factor: battery_mV = adc_mv * (4200.0f / 2430.0f)
#define BAT_DIVIDER_SCALE (4200.0f / 2430.0f) // ≈ 1.728

#define BAT_FULL_MV 4200
#define BAT_EMPTY_MV 3300

esp_err_t BatteryMonitor::init()
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_2,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &_adc_handle);
    if (ret != ESP_OK) {
        // ADC2 may not exist on all P4 revisions; fall back to ADC1 gracefully
        unit_cfg.unit_id = ADC_UNIT_1;
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &_adc_handle));
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12, // 0–3.1V range
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(_adc_handle, BAT_ADC_CHANNEL, &chan_cfg));

    // Calibration (curve fitting preferred on P4)
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_2,
        .chan = BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    _cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &_cali_handle) == ESP_OK);
    if (!_cali_ok) {
        // Fallback: if curve fitting failed (e.g. ADC2 lacks calibration on P4),
        // try ADC1 calibration
        cali_cfg.unit_id = ADC_UNIT_1;
        _cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &_cali_handle) == ESP_OK);
    }

    return ESP_OK;
}

int BatteryMonitor::readMillivolts()
{
    int raw = 0;
    adc_oneshot_read(_adc_handle, BAT_ADC_CHANNEL, &raw);

    int mv_at_pin = 0;
    if (_cali_ok)
    {
        adc_cali_raw_to_voltage(_cali_handle, raw, &mv_at_pin);
    }
    else
    {
        mv_at_pin = (raw * 3100) / 4095;
    }

    // Apply the board's built-in voltage divider scale
    return (int)(mv_at_pin * BAT_DIVIDER_SCALE);
}

int BatteryMonitor::readPercent()
{
    int mv = readMillivolts();
    int pct = (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    return std::clamp(pct, 0, 100);
}

void BatteryMonitor::deinit()
{
    if (_cali_ok)
        adc_cali_delete_scheme_curve_fitting(_cali_handle);
    if (_adc_handle)
        adc_oneshot_del_unit(_adc_handle);
}