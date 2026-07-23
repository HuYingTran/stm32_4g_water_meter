#pragma once
#include <Arduino.h>

// Đo pin Li-ion và phát hiện nguồn Type-C.
namespace Battery {
  void     begin();
  void     update();
  uint16_t millivolts();     // điện áp pin (mV)
  uint8_t  percent();        // dung lượng còn lại (0..100)
  bool     low();            // pin sắp hết
  bool     onExternalPower(); // true khi đang cắm Type-C (DC)
}
