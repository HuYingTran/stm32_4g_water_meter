#pragma once
// ============================================================================
//  config.h  —  Cấu hình trung tâm cho thiết bị đo lưu lượng nước 4G
//  MCU: STM32F103C6T6  |  Modem: A7680C  |  Sensor: YF-S102
// ============================================================================

// ---------------------------------------------------------------------------
//  ĐỊNH DANH THIẾT BỊ
// ---------------------------------------------------------------------------
#define DEVICE_ID          "wm-001"          // ID duy nhất cho mỗi thiết bị

// ---------------------------------------------------------------------------
//  CẤU HÌNH MẠNG / MQTT
// ---------------------------------------------------------------------------
#define SIM_APN            "v-internet"       // APN nhà mạng (Viettel: v-internet)
#define MQTT_BROKER        "tcp://broker.hivemq.com:1883"
#define MQTT_CLIENT_ID     DEVICE_ID
#define MQTT_USER          ""                 // để "" nếu broker không cần auth
#define MQTT_PASS          ""
#define MQTT_KEEPALIVE     60                 // giây
#define MQTT_TOPIC_DATA    "watermeter/" DEVICE_ID "/data"
#define MQTT_TOPIC_STATUS  "watermeter/" DEVICE_ID "/status"

// ---------------------------------------------------------------------------
//  GÁN CHÂN (STM32F103C6T6 - LQFP48)
// ---------------------------------------------------------------------------
// Cảm biến lưu lượng (ngõ ra xung Hall, cần điện trở kéo lên)
#define PIN_FLOW_PULSE     PA0                // ngắt ngoài EXTI0

// Modem A7680C - USART1 (mức logic 3V3)
#define MODEM_SERIAL       Serial1            // PA9=TX -> A7680C RX, PA10=RX <- A7680C TX
#define MODEM_BAUD         115200
#define PIN_MODEM_PWRKEY   PB0                // qua transistor NPN, kéo PWRKEY xuống ~1s

// UART debug - USART2 (PA2=TX, PA3=RX)
#define DEBUG_SERIAL       Serial2
#define DEBUG_BAUD         115200

// Đo pin & phát hiện nguồn Type-C (qua cầu phân áp)
#define PIN_BATT_ADC       PA1
#define PIN_VBUS_DETECT    PA4                // digital: HIGH khi có nguồn Type-C

// LED RGB (cực âm chung, tích cực mức CAO). PA6/PA7/PB1 = TIM3 -> có thể PWM
#define PIN_LED_R          PA6
#define PIN_LED_G          PA7
#define PIN_LED_B          PB1

// ---------------------------------------------------------------------------
//  HIỆU CHUẨN CẢM BIẾN LƯU LƯỢNG (YF-S102)
// ---------------------------------------------------------------------------
// YF-S102: F(Hz) ≈ 7.5 * Q(L/min)  =>  7.5 * 60 = 450 xung / lít
#define FLOW_PULSES_PER_LITER   450.0f
#define FLOW_WINDOW_MS          1000          // cửa sổ đo lưu lượng (1 giây)

// ---------------------------------------------------------------------------
//  ĐO PIN (Li-ion 1 cell)
// ---------------------------------------------------------------------------
#define ADC_VREF_MV        3300              // điện áp tham chiếu ADC (mV)
#define ADC_MAX            4095              // ADC 12-bit
// Cầu phân áp: Vadc = Vbatt * R2/(R1+R2). Với R1=R2 -> hệ số 2.0
#define BATT_DIVIDER       2.0f
#define BATT_FULL_MV       4200              // 100%
#define BATT_EMPTY_MV      3300              // 0%
#define BATT_LOW_PCT       15                // ngưỡng "pin sắp hết" -> LED đỏ nháy

// ---------------------------------------------------------------------------
//  LOGIC GỬI DỮ LIỆU
// ---------------------------------------------------------------------------
#define SEND_INTERVAL_MS       5000          // gửi định kỳ 5s khi đang có nước
#define FLOW_ZERO_EPS          0.05f         // < ngưỡng này coi như lưu lượng = 0 (L/min)
#define FLOW_SUDDEN_DELTA      2.0f          // thay đổi đột ngột (L/min) so lần gửi trước

// ---------------------------------------------------------------------------
//  BỘ ĐỆM OFFLINE (khi mất kết nối MQTT)
// ---------------------------------------------------------------------------
#define OFFLINE_BUFFER_SIZE    32            // số bản ghi lưu tạm trong RAM

// ---------------------------------------------------------------------------
//  TIMEOUT MODEM
// ---------------------------------------------------------------------------
#define MODEM_CMD_TIMEOUT      3000          // ms cho lệnh AT thường
#define MODEM_NET_TIMEOUT      30000         // ms chờ đăng ký mạng
#define MODEM_RETRY_MS         15000         // chu kỳ thử kết nối lại khi mất mạng
