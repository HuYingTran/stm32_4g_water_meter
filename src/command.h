#pragma once
#include <Arduino.h>

// Xử lý bản tin lệnh nhận từ topic watermeter/<id>/cmd.
namespace Command {
  // Phân tích payload JSON và ĐƯA VÀO HÀNG ĐỢI (không gửi ngay).
  //   Hỗ trợ:  {"cmd":"sms","to":["+84…","+84…"],"text":"…"}
  //   Lệnh có "cmd" không nhận dạng được -> bỏ qua (theo đặc tả).
  // Dùng làm callback cho Modem::onMessage() — chạy nhanh, không chặn.
  void handle(const char *topic, const char *payload);

  // Còn yêu cầu SMS nào đang chờ gửi không?
  bool hasSms();

  // Lấy (FIFO) 1 yêu cầu SMS đang chờ: số nhận + nội dung. true nếu có.
  // Việc gửi thực tế do lớp trên (loop) thực hiện để STM32 vẫn đo/đệm được.
  bool nextSms(char *number, size_t nlen, char *text, size_t tlen);
}
