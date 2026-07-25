#include "modem_a7680c.h"
#include "config.h"

namespace {
  // Bộ đệm nhận GỘP: chứa phản hồi lệnh AT. Các khối URC bản tin subscribe
  // (+CMQTTRX…) được TÁCH ra và giao cho callback ngay khi nhận đủ, nên dù
  // /cmd tới xen giữa lúc đang chờ phản hồi lệnh khác cũng không bị mất (vấn đề B).
  char     g_resp[512];
  uint16_t g_respLen = 0;
  bool g_moduleError = true;
  bool g_netReady    = false;
  bool g_mqttUp      = false;
  int  g_rssi        = 99;
  Modem::RxCallback   g_rxCb   = nullptr;
  Modem::IdleCallback g_idleCb = nullptr;   // bơm "rảnh" khi chờ AT lâu (gửi SMS)

  // Khai báo trước (định nghĩa ở phần dưới của namespace).
  void copySpan(char *dest, size_t n, const char *start, const char *end);
  void pumpRx();             // đọc byte đang có + tách URC
  void prepareForCommand();  // dọn buffer trước khi gửi lệnh mới (giữ URC dở dang)

  void dbg(const char *s) {
  #ifdef DEBUG_SERIAL
    DEBUG_SERIAL.println(s);
  #endif
  }

  // Đọc từ modem đến khi thấy `expect`, gặp "ERROR", hoặc hết timeout.
  // Vừa đọc vừa tách URC /cmd nếu có. Phản hồi (đã bỏ URC) nằm trong g_resp.
  bool readUntil(const char *expect, uint32_t timeout) {
    uint32_t start = millis();
    while (millis() - start < timeout) {
      pumpRx();                                       // đọc + tách URC xen giữa
      if (expect && strstr(g_resp, expect)) return true;
      if (strstr(g_resp, "ERROR"))          return false;
      if (g_idleCb) g_idleCb();   // để lớp trên đo & đệm số đo khi modem bận
      delay(2);
    }
    return false;
  }

  // Gửi 1 lệnh AT rồi chờ `expect` (mặc định "OK").
  bool sendAT(const char *cmd, const char *expect = "OK",
              uint32_t timeout = MODEM_CMD_TIMEOUT) {
    prepareForCommand();   // xử lý URC đã đủ + bỏ rác cũ (giữ URC đang tới dở)
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

  // Sao chép chuỗi nằm trong cặp dấu " " đầu tiên tìm thấy từ `src`.
  // Trả về con trỏ ngay sau dấu " đóng (để lấy tiếp cặp kế), hoặc nullptr.
  const char *copyQuoted(char *dest, size_t n, const char *src) {
    if (n == 0) return nullptr;
    const char *q1 = src ? strchr(src, '"') : nullptr;
    const char *q2 = q1 ? strchr(q1 + 1, '"') : nullptr;
    if (!q2) { dest[0] = '\0'; return nullptr; }
    size_t len = (size_t)(q2 - q1 - 1);
    if (len > n - 1) len = n - 1;
    memcpy(dest, q1 + 1, len);
    dest[len] = '\0';
    return q2 + 1;
  }

  // Lấy dãy chữ số đầu tiên trong `src` (bỏ nhãn "+ICCID:", CR/LF, "OK"…).
  // Dùng cho AT+CICCID / AT+CIMI / AT+CGSN (modem trả giá trị số).
  void copyDigits(char *dest, size_t n, const char *src) {
    dest[0] = '\0';
    if (n == 0 || !src) return;
    while (*src && (*src < '0' || *src > '9')) src++;   // tới chữ số đầu
    size_t i = 0;
    while (*src >= '0' && *src <= '9' && i < n - 1) dest[i++] = *src++;
    dest[i] = '\0';
  }

  // Sao chép đoạn [start,end), cắt bỏ CR/LF thừa ở cuối.
  void copySpan(char *dest, size_t n, const char *start, const char *end) {
    if (n == 0) return;
    size_t len = (start && end && end > start) ? (size_t)(end - start) : 0;
    while (len > 0 && (start[len - 1] == '\r' || start[len - 1] == '\n')) len--;
    if (len > n - 1) len = n - 1;
    if (len) memcpy(dest, start, len);
    dest[len] = '\0';
  }

  // Xoá n byte tại vị trí `at` trong g_resp (dịch phần sau lên).
  void respErase(uint16_t at, uint16_t n) {
    if (at >= g_respLen) return;
    if (at + n > g_respLen) n = g_respLen - at;
    memmove(g_resp + at, g_resp + at + n, g_respLen - (at + n));
    g_respLen -= n;
    g_resp[g_respLen] = '\0';
  }

  // Tách mọi bản tin subscribe (+CMQTTRX…) đã nhận ĐỦ trong g_resp: lấy topic +
  // payload giao cho callback, rồi xoá khối đó khỏi g_resp. Lặp cho hết.
  //   +CMQTTRXSTART: 0,<tl>,<pl>\r\n
  //   +CMQTTRXTOPIC: 0,<tl>\r\n<topic>\r\n
  //   +CMQTTRXPAYLOAD: 0,<pl>\r\n<payload>\r\n
  //   +CMQTTRXEND: 0\r\n
  void extractUrc() {
    for (;;) {
      char *tk = strstr(g_resp, "+CMQTTRXTOPIC:");
      if (!tk) return;
      char *pl = strstr(tk, "+CMQTTRXPAYLOAD:");
      char *en = pl ? strstr(pl, "+CMQTTRXEND") : nullptr;
      if (!pl || !en) return;                       // chưa nhận đủ -> chờ thêm

      char *ts = strchr(tk, '\n'); if (ts) ts++;    // đầu chuỗi topic
      char *ps = strchr(pl, '\n'); if (ps) ps++;    // đầu chuỗi payload

      static char topic[64];
      static char payload[256];
      copySpan(topic,   sizeof(topic),   ts, pl);
      copySpan(payload, sizeof(payload), ps, en);
      if (g_rxCb && topic[0] && payload[0]) g_rxCb(topic, payload);

      // Xoá trọn khối: từ +CMQTTRXSTART (nếu có) tới hết dòng +CMQTTRXEND.
      char *st = strstr(g_resp, "+CMQTTRXSTART");
      uint16_t from = (st && st < tk) ? (uint16_t)(st - g_resp) : (uint16_t)(tk - g_resp);
      char *endLine = strchr(en, '\n');
      uint16_t to = endLine ? (uint16_t)(endLine + 1 - g_resp) : g_respLen;
      respErase(from, to - from);
    }
  }

  // Đọc hết byte đang có từ modem vào g_resp rồi tách URC đã đủ.
  void pumpRx() {
    bool got = false;
    while (MODEM_SERIAL.available()) {
      char c = (char)MODEM_SERIAL.read();
      if (g_respLen < sizeof(g_resp) - 1) { g_resp[g_respLen++] = c; g_resp[g_respLen] = '\0'; }
      got = true;
    }
    if (got) extractUrc();
  }

  // Trước khi gửi lệnh mới: xử lý URC đã đủ, rồi bỏ phần "rác" phản hồi cũ.
  // Nếu còn 1 URC đang tới DỞ DANG thì giữ lại (sẽ hoàn tất ở readUntil kế tiếp).
  void prepareForCommand() {
    pumpRx();
    char *st = strstr(g_resp, "+CMQTTRX");
    if (st) {
      uint16_t off = (uint16_t)(st - g_resp);
      if (off) respErase(0, off);          // bỏ rác trước URC dở, giữ URC dở
    } else {
      g_respLen = 0; g_resp[0] = '\0';      // không có URC -> xả sạch
    }
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
  if (!sendAT(cmd)) {
    // ACCQ lỗi = client index 0 còn kẹt từ kết nối trước (mất mạng/publish lỗi).
    // Ngắt + giải phóng để cấp phát lại từ trạng thái sạch, tránh deadlock reconnect.
    sendAT("AT+CMQTTDISC=0,60", "OK", 10000);   // ngắt nếu còn đang "connected"
    sendAT("AT+CMQTTREL=0");                     // giải phóng client index 0
    sendAT(cmd);                                 // cấp phát lại
  }

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

bool Modem::mqttPublish(const char *topic, const char *payload, bool retain) {
  if (!g_mqttUp && !mqttConnect()) return false;

  char cmd[64];

  // Bất kỳ bước nào lỗi -> coi như rớt kết nối (g_mqttUp=false) để lần sau
  // reconnect từ trạng thái sạch, tránh kẹt nửa chừng không bao giờ phục hồi.

  // 1) Nạp topic
  snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%u", (unsigned)strlen(topic));
  if (!sendAT(cmd, ">", 3000)) { g_mqttUp = false; return false; }
  MODEM_SERIAL.print(topic);
  if (!readUntil("OK", 3000))  { g_mqttUp = false; return false; }

  // 2) Nạp payload
  snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%u", (unsigned)strlen(payload));
  if (!sendAT(cmd, ">", 3000)) { g_mqttUp = false; return false; }
  MODEM_SERIAL.print(payload);
  if (!readUntil("OK", 3000))  { g_mqttUp = false; return false; }

  // 3) Publish (QoS1). Tham số thứ 4 = retain (1 cho /info).
  //    AT+CMQTTPUB=<idx>,<qos>,<pub_timeout>[,<retain>]
  const char *pubCmd = retain ? "AT+CMQTTPUB=0,1,60,1" : "AT+CMQTTPUB=0,1,60";
  if (!sendAT(pubCmd, "+CMQTTPUB: 0,0", 10000)) {
    // publish lỗi -> coi như rớt kết nối để lần sau reconnect
    g_mqttUp = false;
    return false;
  }
  return true;
}

bool Modem::mqttSubscribe(const char *topic, int qos) {
  if (!g_mqttUp && !mqttConnect()) return false;

  char cmd[64];
  // Nạp topic đăng ký rồi kích hoạt subscribe (giống trình tự publish).
  snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=0,%u,%d",
           (unsigned)strlen(topic), qos);
  if (!sendAT(cmd, ">", 3000)) return false;
  MODEM_SERIAL.print(topic);
  if (!readUntil("OK", 3000)) return false;

  if (!sendAT("AT+CMQTTSUB=0", "+CMQTTSUB: 0,0", 10000)) {
    dbg("MQTT subscribe fail");
    return false;
  }
  dbg("MQTT subscribed /cmd");
  return true;
}

void Modem::onMessage(Modem::RxCallback cb) { g_rxCb = cb; }
void Modem::onIdle(Modem::IdleCallback cb)  { g_idleCb = cb; }

void Modem::poll() {
  if (g_rxCb == nullptr) return;

  pumpRx();   // đọc byte đang có + tách/giao mọi bản tin /cmd đã nhận đủ

  // Dọn phần "rác" không phải URC để g_resp không phình dần giữa các bản tin
  // (giữ lại nếu đang có một URC tới dở dang).
  if (!strstr(g_resp, "+CMQTTRX")) { g_respLen = 0; g_resp[0] = '\0'; }
}

bool Modem::querySimInfo(Modem::SimInfo &out) {
  if (g_moduleError) return false;
  memset(&out, 0, sizeof(out));

  // Nhà mạng:  +COPS: 0,0,"Viettel",7
  if (sendAT("AT+COPS?", "+COPS:"))
    copyQuoted(out.operatorName, sizeof(out.operatorName), strstr(g_resp, "+COPS:"));

  // Số thuê bao:  +CNUM: "<alpha>","+8496…",145  (nhiều SIM để trống)
  if (sendAT("AT+CNUM", "OK")) {
    const char *after = copyQuoted(out.phone, sizeof(out.phone),
                                   strstr(g_resp, "+CNUM:"));  // trường alpha
    copyQuoted(out.phone, sizeof(out.phone), after);          // số ở cặp "" kế
  }

  // ICCID:  +ICCID: 8984…
  if (sendAT("AT+CICCID", "+ICCID:"))
    copyDigits(out.iccid, sizeof(out.iccid), strstr(g_resp, "+ICCID:"));

  // IMSI / IMEI: modem trả thẳng dãy số rồi OK
  if (sendAT("AT+CIMI", "OK")) copyDigits(out.imsi, sizeof(out.imsi), g_resp);
  if (sendAT("AT+CGSN", "OK")) copyDigits(out.imei, sizeof(out.imei), g_resp);

  return out.imei[0] != '\0' && out.imsi[0] != '\0';
}

bool Modem::sendSms(const char *number, const char *text) {
  if (g_moduleError) return false;

  sendAT("AT+CMGF=1");             // chế độ text
  sendAT("AT+CSCS=\"GSM\"");       // bảng mã GSM (ASCII, không dấu tiếng Việt)

  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
  if (!sendAT(cmd, ">", 5000)) return false;   // chờ dấu nhắc '>'
  MODEM_SERIAL.print(text);
  MODEM_SERIAL.write(0x1A);                     // Ctrl-Z: chốt và gửi

  bool ok = readUntil("+CMGS:", 60000);         // chờ báo gửi (có thể lâu)
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print("SMS -> "); DEBUG_SERIAL.print(number);
  DEBUG_SERIAL.println(ok ? " OK" : " FAIL");
#endif
  return ok;
}

bool Modem::moduleError()  { return g_moduleError; }
bool Modem::networkReady() { return g_netReady; }
int  Modem::rssi()         { return g_rssi; }
