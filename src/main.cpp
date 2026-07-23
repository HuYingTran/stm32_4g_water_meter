// ============================================================================
//  Thiết bị đo lưu lượng nước 4G
//  STM32F103C6T6 + A7680C + YF-S102 + Li-ion + LED RGB
//
//  Luồng chính:
//   - Đếm xung cảm biến (ngắt) -> tính lưu lượng mỗi 1s
//   - Gửi MQTT: định kỳ 5s khi có nước, khi bắt đầu/kết thúc dòng chảy,
//     và khi lưu lượng thay đổi đột ngột
//   - Mất kết nối -> đệm dữ liệu, tự gửi bù khi có mạng lại
//   - LED RGB báo trạng thái theo thứ tự ưu tiên
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "flow_sensor.h"
#include "battery.h"
#include "led_status.h"
#include "data_buffer.h"
#include "modem_a7680c.h"

// ---- Trạng thái gửi dữ liệu ----
static float    g_lastSentFlow = 0.0f;
static bool     g_wasFlowing   = false;
static uint32_t g_lastPeriodic = 0;
static uint32_t g_lastModemTry = 0;
static uint32_t g_lastBattTs   = 0;

// ---- Đổi float -> chuỗi 2 chữ số thập phân (tránh dùng printf float) ----
static void fmt2(char *out, float v) {
  if (v < 0) { *out++ = '-'; v = -v; }
  long scaled = (long)(v * 100.0f + 0.5f);
  long ip = scaled / 100;
  long fp = scaled % 100;
  sprintf(out, "%ld.%02ld", ip, fp);
}

// ---- Dựng payload JSON ----
static void buildPayload(char *buf, size_t n, float flow, float total,
                         uint8_t batt, bool dc, int rssi) {
  char fs[16], ts[16];
  fmt2(fs, flow);
  fmt2(ts, total);
  snprintf(buf, n,
           "{\"id\":\"%s\",\"flow\":%s,\"total\":%s,\"batt\":%u,"
           "\"pwr\":\"%s\",\"rssi\":%d}",
           DEVICE_ID, fs, ts, batt, dc ? "dc" : "bat", rssi);
}

// Gửi 1 bản ghi; nếu không gửi được thì đưa vào bộ đệm offline.
static void sendOrBuffer(float flow, float total, uint8_t batt, bool dc) {
  char payload[128];
  buildPayload(payload, sizeof(payload), flow, total, batt, dc, Modem::rssi());

  if (Modem::mqttConnected() && Modem::mqttPublish(MQTT_TOPIC_DATA, payload)) {
    g_lastSentFlow = flow;
    return;
  }
  // Không gửi được -> lưu tạm
  DataBuffer::Record r{ millis(), flow, total, batt, dc };
  DataBuffer::push(r);
  g_lastSentFlow = flow;   // vẫn cập nhật mốc so sánh "đột ngột"
}

// Gửi bù dữ liệu đã đệm khi có kết nối trở lại.
static void flushBuffer() {
  char payload[128];
  while (!DataBuffer::empty() && Modem::mqttConnected()) {
    DataBuffer::Record r;
    if (!DataBuffer::peek(r)) break;
    buildPayload(payload, sizeof(payload), r.flowLpm, r.total, r.batt, r.dc,
                 Modem::rssi());
    if (Modem::mqttPublish(MQTT_TOPIC_DATA, payload)) {
      DataBuffer::pop();
    } else {
      break;   // vẫn lỗi -> để lần sau
    }
  }
}

// Chọn trạng thái LED theo thứ tự ưu tiên.
static void updateLed(float flow, uint32_t now) {
  Led::Status s;
  if (FlowSensor::sensorError())          s = Led::SENSOR_ERROR;   // đỏ
  else if (Battery::low())                s = Led::BATTERY_LOW;    // đỏ nháy
  else if (Modem::moduleError())          s = Led::SIM_ERROR;      // vàng nháy
  else if (!Modem::networkReady())        s = Led::SIM_NO_DATA;    // vàng
  else if (!Modem::mqttConnected() &&
           !DataBuffer::empty())          s = Led::NO_MQTT;        // lá nháy
  else if (flow > FLOW_ZERO_EPS)          s = Led::FLOWING_OK;     // lá
  else                                    s = Led::NO_WATER;       // dương
  Led::set(s);
  Led::update(now);
}

// Cố gắng đảm bảo có mạng + MQTT (không chặn quá lâu, có chu kỳ thử lại).
static void ensureConnectivity(uint32_t now) {
  if (Modem::mqttConnected()) return;
  if (now - g_lastModemTry < MODEM_RETRY_MS) return;
  g_lastModemTry = now;

  if (Modem::moduleError()) { Modem::powerOn(); }
  if (Modem::moduleError()) return;

  if (!Modem::networkReady()) { Modem::bringUpNetwork(); }
  if (!Modem::networkReady()) return;

  Modem::mqttConnect();
}

void setup() {
  DEBUG_SERIAL.begin(DEBUG_BAUD);
  DEBUG_SERIAL.println(F("\n=== Water Meter 4G khoi dong ==="));

  Led::begin();
  Led::set(Led::NO_WATER);

  FlowSensor::begin();
  Battery::begin();
  DataBuffer::begin();

  Modem::begin();
  Modem::powerOn();
  if (Modem::bringUpNetwork()) {
    Modem::mqttConnect();
  }
  g_lastModemTry = millis();
  g_lastPeriodic = millis();
}

void loop() {
  uint32_t now = millis();

  // 1) Cập nhật pin/nguồn định kỳ (2s/lần)
  if (now - g_lastBattTs >= 2000) {
    g_lastBattTs = now;
    Battery::update();
  }

  // 2) Cập nhật lưu lượng theo cửa sổ đo
  if (FlowSensor::update(now)) {
    float flow  = FlowSensor::flowLpm();
    float total = FlowSensor::totalLiters();
    if (flow < FLOW_ZERO_EPS) flow = 0.0f;

    bool flowing = flow > 0.0f;
    bool doSend  = false;

    if (flowing && !g_wasFlowing) doSend = true;               // bắt đầu có nước
    if (!flowing && g_wasFlowing) doSend = true;               // dòng chảy về 0
    if (fabsf(flow - g_lastSentFlow) >= FLOW_SUDDEN_DELTA)     // thay đổi đột ngột
      doSend = true;
    if (flowing && (now - g_lastPeriodic >= SEND_INTERVAL_MS)) // định kỳ 5s
      doSend = true;

    if (doSend) {
      sendOrBuffer(flow, total, Battery::percent(), Battery::onExternalPower());
      g_lastPeriodic = now;
    }
    g_wasFlowing = flowing;
  }

  // 3) Bảo đảm kết nối + gửi bù dữ liệu đệm
  ensureConnectivity(now);
  if (Modem::mqttConnected() && !DataBuffer::empty()) flushBuffer();

  // 4) LED trạng thái
  updateLed(FlowSensor::flowLpm() < FLOW_ZERO_EPS ? 0.0f : FlowSensor::flowLpm(),
            now);
}
