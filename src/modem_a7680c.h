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
  // retain=true dùng cho topic /info (broker giữ lại bản tin gần nhất).
  bool mqttPublish(const char *topic, const char *payload, bool retain = false);

  // Đăng ký nhận (subscribe) 1 topic. Cần gọi lại sau mỗi lần kết nối lại.
  bool mqttSubscribe(const char *topic, int qos);

  // Callback khi có bản tin tới trên topic đã subscribe.
  typedef void (*RxCallback)(const char *topic, const char *payload);
  void onMessage(RxCallback cb);

  // Callback được gọi định kỳ TRONG LÚC modem đang chờ phản hồi AT lâu
  // (điển hình: đang gửi SMS). Cho phép lớp trên tiếp tục đo lưu lượng và
  // đệm số đo vào RAM trong khi modem tạm "chiếm sóng". Không được gọi lệnh AT.
  typedef void (*IdleCallback)();
  void onIdle(IdleCallback cb);

  // Đọc & xử lý URC đến từ modem (bản tin /cmd). Gọi thường xuyên trong loop().
  void poll();

  // ---- Thông tin SIM thuê bao (phục vụ topic /info) ----
  struct SimInfo {
    char operatorName[32];   // tên nhà mạng (AT+COPS?)
    char phone[20];          // số thuê bao (AT+CNUM, có thể rỗng)
    char iccid[24];          // số serial SIM (AT+CICCID)
    char imsi[20];           // IMSI (AT+CIMI)
    char imei[20];           // IMEI thiết bị (AT+CGSN)
  };
  // Truy vấn thông tin SIM. true nếu lấy được tối thiểu IMEI + IMSI.
  bool querySimInfo(SimInfo &out);

  // ---- SMS ----
  // Gửi 1 SMS text (charset GSM/ASCII) tới `number`. true nếu modem báo +CMGS.
  bool sendSms(const char *number, const char *text);

  // ---- Cờ trạng thái phục vụ LED ----
  bool moduleError();   // modem không phản hồi AT / lỗi phần cứng
  bool networkReady();  // đã đăng ký mạng + PDP active (có data)
  int  rssi();          // CSQ (0..31, 99 = không xác định)
}
