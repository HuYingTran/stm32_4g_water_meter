#pragma once
#include <Arduino.h>

// Điều khiển LED RGB báo trạng thái. Ưu tiên từ cao xuống thấp.
namespace Led {
  enum Status {
    SENSOR_ERROR,   // đỏ           - lỗi không đọc được cảm biến
    BATTERY_LOW,    // đỏ nháy      - pin sắp hết
    SIM_ERROR,      // vàng nháy    - lỗi module SIM
    SIM_NO_DATA,    // vàng         - SIM không có lưu lượng data
    NO_MQTT,        // xanh lá nháy - có dữ liệu nhưng không kết nối được MQTT
    FLOWING_OK,     // xanh lá      - hoạt động ổn định, có nước chảy
    NO_WATER        // xanh dương   - không có nước chảy qua
  };

  void begin();
  void set(Status s);
  void update(uint32_t nowMs);   // xử lý nhấp nháy, gọi liên tục trong loop
}
