#include "battery.h"
#include "config.h"

namespace {
  uint16_t g_mv = 0;
  uint8_t  g_pct = 0;
  bool     g_dc = false;

  // Lọc trung bình trượt đơn giản để giảm nhiễu ADC.
  uint32_t readAvgAdc(uint8_t pin, uint8_t samples = 8) {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < samples; i++) acc += analogRead(pin);
    return acc / samples;
  }
}

void Battery::begin() {
  analogReadResolution(12);
  pinMode(PIN_BATT_ADC, INPUT_ANALOG);
  pinMode(PIN_VBUS_DETECT, INPUT);
  update();
}

void Battery::update() {
  uint32_t adc = readAvgAdc(PIN_BATT_ADC);
  // Vadc(mV) = adc/ADC_MAX * VREF ; Vbatt = Vadc * hệ số phân áp
  uint32_t vadc = (adc * ADC_VREF_MV) / ADC_MAX;
  g_mv = (uint16_t)(vadc * BATT_DIVIDER);

  // Ánh xạ tuyến tính sang % (kẹp trong [0,100])
  int32_t pct = ((int32_t)g_mv - BATT_EMPTY_MV) * 100 /
                (BATT_FULL_MV - BATT_EMPTY_MV);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  g_pct = (uint8_t)pct;

  g_dc = digitalRead(PIN_VBUS_DETECT) == HIGH;
}

uint16_t Battery::millivolts()     { return g_mv; }
uint8_t  Battery::percent()        { return g_pct; }
bool     Battery::low()            { return g_pct <= BATT_LOW_PCT && !g_dc; }
bool     Battery::onExternalPower(){ return g_dc; }
