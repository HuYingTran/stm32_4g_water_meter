#include "modem_a7680c.h"
#include "config.h"

namespace {
  char g_resp[256];          // đệm phản hồi AT dùng chung
  bool g_moduleError = true;
  bool g_netReady    = false;
  bool g_mqttUp      = false;
  int  g_rssi        = 99;

  void dbg(const char *s) {
  #ifdef DEBUG_SERIAL
    DEBUG_SERIAL.println(s);
  #endif
  }

  // Đọc từ modem đến khi thấy `expect`, gặp "ERROR", hoặc hết timeout.
  // Toàn bộ phản hồi lưu vào g_resp. Trả về true nếu thấy `expect`.
  bool readUntil(const char *expect, uint32_t timeout) {
    uint16_t idx = 0;
    g_resp[0] = '\0';
    uint32_t start = millis();
    while (millis() - start < timeout) {
      while (MODEM_SERIAL.available()) {
        char c = (char)MODEM_SERIAL.read();
        if (idx < sizeof(g_resp) - 1) { g_resp[idx++] = c; g_resp[idx] = '\0'; }
        if (expect && strstr(g_resp, expect)) return true;
        if (strstr(g_resp, "ERROR"))          return false;
      }
      delay(2);
    }
    return false;
  }

  // Gửi 1 lệnh AT rồi chờ `expect` (mặc định "OK").
  bool sendAT(const char *cmd, const char *expect = "OK",
              uint32_t timeout = MODEM_CMD_TIMEOUT) {
    while (MODEM_SERIAL.available()) MODEM_SERIAL.read();   // xả rác cũ
    MODEM_SERIAL.println(cmd);
  #ifdef DEBUG_SERIAL
    DEBUG_SERIAL.print(">> "); DEBUG_SERIAL.println(cmd);
  #endif
    bool ok = readUntil(expect, timeout);
  #ifdef DEBUG_SERIAL
    DEBUG_SERIAL.print("<< "); DEBUG_SERIAL.println(g_resp);
  #endif
    return ok;
  }

  // Lấy CSQ -> cập nhật g_rssi. true nếu có sóng hợp lệ.
  bool queryRssi() {
    if (!sendAT("AT+CSQ", "+CSQ:")) return false;
    char *p = strstr(g_resp, "+CSQ:");
    if (!p) return false;
    g_rssi = atoi(p + 5);
    return g_rssi != 99;
  }

  // Đã đăng ký mạng? (CREG/CGREG stat = 1 hoặc 5)
  bool registered() {
    if (!sendAT("AT+CGREG?", "+CGREG:")) return false;
    char *p = strstr(g_resp, "+CGREG:");
    if (!p) return false;
    // định dạng: +CGREG: <n>,<stat>
    char *comma = strchr(p, ',');
    if (!comma) return false;
    int stat = atoi(comma + 1);
    return stat == 1 || stat == 5;
  }
}

void Modem::begin() {
  MODEM_SERIAL.begin(MODEM_BAUD);
  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
}

bool Modem::powerOn() {
  // Thử "đánh thức" bằng AT trước; nếu modem đã bật thì bỏ qua PWRKEY.
  for (int i = 0; i < 3; i++) {
    if (sendAT("AT", "OK", 1000)) { g_moduleError = false; break; }
    g_moduleError = true;
  }

  if (g_moduleError) {
    // Xung PWRKEY: kéo cao ~1.2s (qua NPN sẽ kéo PWRKEY của modem xuống)
    dbg("Modem: pulse PWRKEY");
    digitalWrite(PIN_MODEM_PWRKEY, HIGH);
    delay(1200);
    digitalWrite(PIN_MODEM_PWRKEY, LOW);
    // A7680C cần vài giây để khởi động
    for (int i = 0; i < 20; i++) {
      if (sendAT("AT", "OK", 1000)) { g_moduleError = false; break; }
      delay(500);
    }
  }

  if (!g_moduleError) {
    sendAT("ATE0");                 // tắt echo cho gọn
    sendAT("AT+CMEE=1");            // báo lỗi dạng số
  }
  return !g_moduleError;
}

bool Modem::bringUpNetwork() {
  g_netReady = false;
  if (g_moduleError) return false;

  // SIM sẵn sàng?
  if (!sendAT("AT+CPIN?", "+CPIN: READY", 5000)) { dbg("SIM chưa sẵn sàng"); return false; }

  queryRssi();   // cập nhật sóng (dùng cho LED dù chưa có data)

  // Chờ đăng ký mạng
  uint32_t start = millis();
  while (millis() - start < MODEM_NET_TIMEOUT) {
    if (registered()) break;
    delay(1000);
  }
  if (!registered()) { dbg("Chưa đăng ký mạng"); return false; }

  // Cấu hình APN + kích hoạt PDP context
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", SIM_APN);
  sendAT(cmd);
  if (!sendAT("AT+CGACT=1,1", "OK", 15000)) { dbg("PDP active fail"); return false; }

  g_netReady = true;
  return true;
}

bool Modem::mqttConnect() {
  if (!g_netReady) return false;
  if (g_mqttUp)    return true;

  sendAT("AT+CMQTTSTART", "OK", 10000);        // khởi động dịch vụ MQTT (OK cả khi đã start)

  char cmd[96];
  snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",0", MQTT_CLIENT_ID);
  sendAT(cmd);

  if (strlen(MQTT_USER) > 0) {
    snprintf(cmd, sizeof(cmd),
             "AT+CMQTTCONNECT=0,\"%s\",%d,1,\"%s\",\"%s\"",
             MQTT_BROKER, MQTT_KEEPALIVE, MQTT_USER, MQTT_PASS);
  } else {
    snprintf(cmd, sizeof(cmd),
             "AT+CMQTTCONNECT=0,\"%s\",%d,1",
             MQTT_BROKER, MQTT_KEEPALIVE);
  }
  // Kết quả thành công: URC "+CMQTTCONNECT: 0,0"
  if (sendAT(cmd, "+CMQTTCONNECT: 0,0", 15000)) {
    g_mqttUp = true;
    dbg("MQTT connected");
  } else {
    g_mqttUp = false;
    dbg("MQTT connect fail");
  }
  return g_mqttUp;
}

void Modem::mqttDisconnect() {
  if (!g_mqttUp) return;
  sendAT("AT+CMQTTDISC=0,120", "OK", 10000);
  sendAT("AT+CMQTTREL=0");
  sendAT("AT+CMQTTSTOP", "OK", 10000);
  g_mqttUp = false;
}

bool Modem::mqttConnected() { return g_mqttUp; }

bool Modem::mqttPublish(const char *topic, const char *payload) {
  if (!g_mqttUp && !mqttConnect()) return false;

  char cmd[64];

  // 1) Nạp topic
  snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%u", (unsigned)strlen(topic));
  if (!sendAT(cmd, ">", 3000)) return false;
  MODEM_SERIAL.print(topic);
  if (!readUntil("OK", 3000)) return false;

  // 2) Nạp payload
  snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%u", (unsigned)strlen(payload));
  if (!sendAT(cmd, ">", 3000)) return false;
  MODEM_SERIAL.print(payload);
  if (!readUntil("OK", 3000)) return false;

  // 3) Publish (QoS1)
  if (!sendAT("AT+CMQTTPUB=0,1,60", "+CMQTTPUB: 0,0", 10000)) {
    // publish lỗi -> coi như rớt kết nối để lần sau reconnect
    g_mqttUp = false;
    return false;
  }
  return true;
}

bool Modem::moduleError()  { return g_moduleError; }
bool Modem::networkReady() { return g_netReady; }
int  Modem::rssi()         { return g_rssi; }
