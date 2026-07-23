#pragma once
#include <Arduino.h>

// Cảm biến lưu lượng YF-S102: đếm xung bằng ngắt ngoài.
namespace FlowSensor {
  void  begin();
  // Cập nhật theo cửa sổ FLOW_WINDOW_MS. Trả về true khi có kết quả mới.
  bool  update(uint32_t nowMs);
  float flowLpm();          // lưu lượng hiện tại (lít/phút)
  float totalLiters();      // tổng lượng nước tích luỹ (lít)
  bool  sensorError();      // true khi nghi ngờ cảm biến lỗi
}
