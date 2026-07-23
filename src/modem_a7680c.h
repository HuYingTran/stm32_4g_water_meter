#pragma once
#include <Arduino.h>

// Driver modem SIMCom A7680C qua tập lệnh AT (bao gồm MQTT nội bộ).
namespace Modem {
  void begin();

  // Bật nguồn modem (xung PWRKEY) và chờ phản hồi AT. true nếu modem sống.
  bool powerOn();

  // Kiểm tra SIM + đăng ký mạng + kích hoạt PDP (APN). Cập nhật cờ trạng thái.
  bool bringUpNetwork();

  // Kết nối MQTT tới broker. true nếu thành công.
  bool mqttConnect();
  void mqttDisconnect();
  bool mqttConnected();

  // Publish 1 message. Tự thử kết nối lại nếu rớt. true nếu gửi xong.
  bool mqttPublish(const char *topic, const char *payload);

  // ---- Cờ trạng thái phục vụ LED ----
  bool moduleError();   // modem không phản hồi AT / lỗi phần cứng
  bool networkReady();  // đã đăng ký mạng + PDP active (có data)
  int  rssi();          // CSQ (0..31, 99 = không xác định)
}
