#include "command.h"
#include "config.h"
#include <Arduino.h>

namespace {
  // ---- Hàng đợi SMS chờ gửi (vòng, trong RAM) ----
  // loop() sẽ rút lần lượt để gửi; trong lúc đó STM32 vẫn đo & đệm số đo.
  struct SmsReq {
    char to[20];
    char text[SMS_MAX_TEXT_LEN + 1];
  };
  const uint8_t QCAP = SMS_MAX_RECIPIENTS + 1;   // đủ 1 lệnh đầy + biên
  SmsReq  g_q[QCAP];
  uint8_t g_head = 0, g_tail = 0, g_count = 0;

  bool enqueue(const char *to, const char *text) {
    if (g_count >= QCAP) return false;           // đầy -> bỏ yêu cầu mới
    strncpy(g_q[g_tail].to,   to,   sizeof(g_q[g_tail].to) - 1);
    g_q[g_tail].to[sizeof(g_q[g_tail].to) - 1]     = '\0';
    strncpy(g_q[g_tail].text, text, sizeof(g_q[g_tail].text) - 1);
    g_q[g_tail].text[sizeof(g_q[g_tail].text) - 1] = '\0';
    g_tail = (g_tail + 1) % QCAP;
    g_count++;
    return true;
  }

  // Trích giá trị chuỗi của "key" trong JSON phẳng. true nếu tìm thấy.
  // Xử lý escape tối thiểu: \" \\ \/ \n \t.
  bool jsonString(const char *json, const char *key, char *dest, size_t n) {
    if (n == 0) return false;
    dest[0] = '\0';

    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');     // tới dấu ':'
    if (!p) return false;
    p = strchr(p, '"');                   // dấu " mở của value
    if (!p) return false;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i < n - 1) {
      if (*p == '\\' && p[1]) {
        p++;
        switch (*p) {
          case 'n': dest[i++] = '\n'; break;
          case 't': dest[i++] = '\t'; break;
          default:  dest[i++] = *p;   break;   // \" \\ \/ …
        }
      } else {
        dest[i++] = *p;
      }
      p++;
    }
    dest[i] = '\0';
    return true;
  }
}

bool Command::hasSms() { return g_count > 0; }

bool Command::nextSms(char *number, size_t nlen, char *text, size_t tlen) {
  if (g_count == 0) return false;
  if (number && nlen) {
    strncpy(number, g_q[g_head].to, nlen - 1);
    number[nlen - 1] = '\0';
  }
  if (text && tlen) {
    strncpy(text, g_q[g_head].text, tlen - 1);
    text[tlen - 1] = '\0';
  }
  g_head = (g_head + 1) % QCAP;
  g_count--;
  return true;
}

void Command::handle(const char *topic, const char *payload) {
  (void)topic;

  char cmd[16];
  if (!jsonString(payload, "cmd", cmd, sizeof(cmd))) return;

  if (strcmp(cmd, "sms") == 0) {
    char text[SMS_MAX_TEXT_LEN + 1];
    if (!jsonString(payload, "text", text, sizeof(text))) return;

    // Duyệt mảng "to":[ "num1","num2", … ] và ĐƯA VÀO HÀNG ĐỢI (không gửi ở đây)
    const char *p = strstr(payload, "\"to\"");
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    const char *end = strchr(p, ']');
    if (!end) return;

    int queued = 0;
    while (queued < SMS_MAX_RECIPIENTS) {
      const char *q1 = strchr(p, '"');
      if (!q1 || q1 > end) break;
      const char *q2 = strchr(q1 + 1, '"');
      if (!q2 || q2 > end) break;

      char num[20];
      size_t len = (size_t)(q2 - q1 - 1);
      if (len > sizeof(num) - 1) len = sizeof(num) - 1;
      memcpy(num, q1 + 1, len);
      num[len] = '\0';

      if (num[0]) { enqueue(num, text); queued++; }
      p = q2 + 1;
    }
  }
  // cmd không nhận dạng -> bỏ qua
}
