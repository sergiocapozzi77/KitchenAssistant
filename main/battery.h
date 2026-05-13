// battery.h
#pragma once
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define BAT_ADC_CHANNEL ADC_CHANNEL_4 // adjust to your GPIO
#define BAT_FULL_MV 4200
#define BAT_EMPTY_MV 3300
#define VDIVIDER_RATIO 2.0f // match your resistors

class BatteryMonitor
{
public:
    esp_err_t init();
    int readMillivolts(); // actual battery voltage in mV
    int readPercent();    // 0–100
    void deinit();

private:
    adc_oneshot_unit_handle_t _adc_handle = nullptr;
    adc_cali_handle_t _cali_handle = nullptr;
    bool _cali_ok = false;
};