#include "led_status.h"
#include "config.h"

namespace {
  Led::Status g_status = Led::NO_WATER;
  bool     g_blinkOn = true;
  uint32_t g_blinkTs = 0;
  const uint32_t BLINK_MS = 400;

  inline void rgb(bool r, bool g, bool b) {
    digitalWrite(PIN_LED_R, r ? HIGH : LOW);
    digitalWrite(PIN_LED_G, g ? HIGH : LOW);
    digitalWrite(PIN_LED_B, b ? HIGH : LOW);
  }

  // Trả về màu nền và cờ nhấp nháy cho từng trạng thái.
  void colorFor(Led::Status s, bool &r, bool &g, bool &b, bool &blink) {
    blink = false;
    switch (s) {
      case Led::SENSOR_ERROR: r=1; g=0; b=0;               break; // đỏ
      case Led::BATTERY_LOW:  r=1; g=0; b=0; blink=true;   break; // đỏ nháy
      case Led::SIM_ERROR:    r=1; g=1; b=0; blink=true;   break; // vàng nháy
      case Led::SIM_NO_DATA:  r=1; g=1; b=0;               break; // vàng
      case Led::NO_MQTT:      r=0; g=1; b=0; blink=true;   break; // lá nháy
      case Led::FLOWING_OK:   r=0; g=1; b=0;               break; // lá
      case Led::NO_WATER:     r=0; g=0; b=1;               break; // dương
      default:                r=0; g=0; b=0;               break;
    }
  }
}

void Led::begin() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  rgb(0, 0, 0);
}

void Led::set(Status s) {
  if (s != g_status) {
    g_status = s;
    g_blinkOn = true;          // bắt đầu chu kỳ nháy ở trạng thái sáng
    g_blinkTs = millis();
  }
}

void Led::update(uint32_t nowMs) {
  bool r, g, b, blink;
  colorFor(g_status, r, g, b, blink);

  if (blink) {
    if (nowMs - g_blinkTs >= BLINK_MS) {
      g_blinkTs = nowMs;
      g_blinkOn = !g_blinkOn;
    }
    if (!g_blinkOn) { rgb(0, 0, 0); return; }
  }
  rgb(r, g, b);
}
