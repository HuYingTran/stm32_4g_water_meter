#pragma once
#include <Arduino.h>

// Bộ đệm vòng lưu bản ghi khi mất kết nối MQTT.
// Lưu trong RAM (mất khi reset). Dung lượng: OFFLINE_BUFFER_SIZE bản ghi.
namespace DataBuffer {
  struct Record {
    uint32_t ts;       // millis lúc ghi (mốc tương đối)
    float    flowLpm;
    float    total;
    uint8_t  batt;
    bool     dc;       // đang cắm nguồn DC?
  };

  void   begin();
  bool   push(const Record &r);   // false nếu đầy (ghi đè bản ghi cũ nhất)
  bool   peek(Record &r);         // đọc bản ghi cũ nhất (không xoá)
  void   pop();                   // xoá bản ghi cũ nhất sau khi gửi thành công
  bool   empty();
  bool   full();
  uint8_t count();
}
