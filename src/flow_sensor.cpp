#include "flow_sensor.h"
#include "config.h"

namespace {
  volatile uint32_t g_pulseCount = 0;   // đếm trong ISR
  uint32_t g_windowStart = 0;
  uint32_t g_lastPulseSnapshot = 0;

  float    g_flowLpm = 0.0f;
  float    g_totalLiters = 0.0f;
  bool     g_sensorError = false;

  // Nếu không có xung nào trong nhiều cửa sổ liên tiếp -> có thể lỗi cảm biến.
  // Lưu ý: "không có nước" cũng ra 0 xung nên KHÔNG tự động báo lỗi ở đây;
  // việc phân biệt do lớp trên (main) xử lý cùng trạng thái đường ống.
  void onPulse() { g_pulseCount++; }
}

void FlowSensor::begin() {
  pinMode(PIN_FLOW_PULSE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_PULSE), onPulse, FALLING);
  g_windowStart = millis();
}

bool FlowSensor::update(uint32_t nowMs) {
  if (nowMs - g_windowStart < FLOW_WINDOW_MS) return false;

  noInterrupts();
  uint32_t count = g_pulseCount;
  g_pulseCount = 0;
  interrupts();

  uint32_t elapsed = nowMs - g_windowStart;
  g_windowStart = nowMs;
  g_lastPulseSnapshot = count;

  // lít trong cửa sổ này
  float liters = count / FLOW_PULSES_PER_LITER;
  g_totalLiters += liters;
  // quy đổi ra lít/phút
  g_flowLpm = liters * (60000.0f / (float)elapsed);

  return true;
}

float FlowSensor::flowLpm()      { return g_flowLpm; }
float FlowSensor::totalLiters()  { return g_totalLiters; }
bool  FlowSensor::sensorError()  { return g_sensorError; }
