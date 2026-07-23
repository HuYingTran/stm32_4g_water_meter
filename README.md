# Thiết bị đo lưu lượng nước 4G (STM32 + A7680C)

Firmware cho thiết bị đo lưu lượng nước, đẩy dữ liệu lên MQTT qua mạng 4G, có
pin dự phòng và LED RGB báo trạng thái.

---

## 1. Phần cứng

| Thành phần            | Linh kiện        | Ghi chú                                   |
|-----------------------|------------------|-------------------------------------------|
| Vi điều khiển         | STM32F103C6T6    | 32KB Flash / 10KB RAM (xem lưu ý bên dưới)|
| Module 4G             | SIMCom A7680C    | Giao tiếp UART, có MQTT nội bộ (lệnh AT)   |
| Cảm biến lưu lượng    | YF-S102          | Ngõ ra xung Hall, ~450 xung/lít           |
| Nguồn                 | Type-C + Pin Li-ion | Chạy DC, tự chuyển sang pin khi mất DC  |
| Sạc pin               | Mạch sạc Li-ion (TP4056…) |                                  |
| Báo trạng thái        | LED RGB          | Cực âm chung, tích cực mức cao             |

> **Lưu ý dung lượng:** STM32F103C6T6 chỉ có **32KB Flash**. Firmware hiện dùng
> ~71% flash nên vẫn vừa, nhưng nếu bạn thêm tính năng và bị tràn, hãy đổi
> `board = genericSTM32F103C8` trong `platformio.ini` (đa số "blue pill" thực tế
> là C8/CB với 64–128KB).

### Sơ đồ chân (LQFP48)

| Chức năng                     | Chân STM32 | Kết nối tới                       |
|-------------------------------|------------|-----------------------------------|
| Xung cảm biến lưu lượng       | `PA0`      | Ngõ OUT của YF-S102 (kéo lên 3V3) |
| UART modem — TX               | `PA9`      | RX của A7680C                     |
| UART modem — RX               | `PA10`     | TX của A7680C                     |
| PWRKEY modem                  | `PB0`      | Qua transistor NPN → PWRKEY       |
| UART debug — TX               | `PA2`      | RX của USB-TTL (log 115200)       |
| UART debug — RX               | `PA3`      | TX của USB-TTL                    |
| Đo điện áp pin (ADC)          | `PA1`      | Cầu phân áp từ + pin              |
| Phát hiện nguồn Type-C        | `PA4`      | HIGH khi có VBUS                  |
| LED đỏ / lá / dương           | `PA6/PA7/PB1` | 3 chân LED RGB (qua điện trở)  |

Tất cả gán chân tập trung ở `include/config.h` — chỉnh ở đó nếu đổi layout.

---

## 2. Nguyên lý hoạt động

- **Đo lưu lượng:** đếm xung cảm biến bằng ngắt ngoài, tính lưu lượng (L/phút)
  và tổng lượng nước (lít) mỗi 1 giây. Hệ số hiệu chuẩn: `450 xung/lít`.
- **Gửi MQTT** khi:
  - Định kỳ **5 giây/lần** trong lúc có nước chảy.
  - **Bắt đầu** có dòng chảy (0 → > 0).
  - **Kết thúc** dòng chảy (> 0 → 0).
  - Lưu lượng **thay đổi đột ngột** (lệch ≥ 2 L/phút so với lần gửi trước).
- **Nội dung gửi:** lưu lượng + tổng nước + dung lượng pin + nguồn + sóng.
- **Nguồn:** chạy bằng Type-C khi có; mất Type-C thì tự chạy pin Li-ion.
- **Offline buffering:** khi không kết nối được MQTT, dữ liệu được lưu vào bộ đệm
  vòng trong RAM (32 bản ghi) và **tự gửi bù** khi có kết nối trở lại.

### Định dạng MQTT

- **Topic dữ liệu:** `watermeter/<device_id>/data`
- **Payload (JSON):**

```json
{"id":"wm-001","flow":12.50,"total":345.60,"batt":87,"pwr":"dc","rssi":19}
```

| Trường  | Ý nghĩa                                    |
|---------|--------------------------------------------|
| `id`    | ID thiết bị                                |
| `flow`  | Lưu lượng tức thời (L/phút)                |
| `total` | Tổng lượng nước tích luỹ (lít)            |
| `batt`  | Dung lượng pin còn lại (%)                 |
| `pwr`   | `dc` = đang cắm Type-C, `bat` = chạy pin   |
| `rssi`  | Cường độ sóng CSQ (0–31, 99 = không rõ)    |

---

## 3. Trạng thái đèn LED

| Màu            | Trạng thái                                          |
|----------------|-----------------------------------------------------|
| 🔴 Đỏ           | Lỗi không đọc được cảm biến                          |
| 🔴 Đỏ nháy      | Pin sắp hết                                          |
| 🟡 Vàng         | SIM không có lưu lượng data (chưa vào được mạng)     |
| 🟡 Vàng nháy    | Lỗi module SIM (không phản hồi AT)                   |
| 🟢 Xanh lá      | Hoạt động ổn định, có nước chảy qua                  |
| 🟢 Xanh lá nháy | Có dữ liệu nhưng không kết nối được MQTT             |
| 🔵 Xanh dương   | Không có nước chảy qua                               |

Thứ tự ưu tiên hiển thị: **Cảm biến lỗi → Pin yếu → Lỗi SIM → SIM không data →
Không MQTT → Có nước → Không nước**.

---

## 4. Cấu trúc mã nguồn

```
include/
  config.h            # gán chân, APN/MQTT, hiệu chuẩn, ngưỡng — chỉnh ở đây
src/
  main.cpp            # vòng lặp chính + máy trạng thái gửi dữ liệu
  flow_sensor.*       # đếm xung cảm biến, tính lưu lượng/tổng
  battery.*           # đo pin + phát hiện nguồn Type-C
  led_status.*        # điều khiển LED RGB theo ưu tiên
  modem_a7680c.*      # driver AT: bật nguồn, mạng/PDP, MQTT publish
  data_buffer.*       # bộ đệm vòng lưu dữ liệu offline
platformio.ini        # cấu hình build
```

---

## 5. Cấu hình

Mở `include/config.h` và chỉnh các mục quan trọng trước khi nạp:

```c
#define DEVICE_ID     "wm-001"                    // ID mỗi thiết bị
#define SIM_APN       "v-internet"                // APN nhà mạng
#define MQTT_BROKER   "tcp://broker.hivemq.com:1883"
#define MQTT_USER     ""                          // để trống nếu không cần auth
#define MQTT_PASS     ""
#define FLOW_PULSES_PER_LITER  450.0f             // hiệu chuẩn cảm biến
#define BATT_DIVIDER  2.0f                         // theo cầu phân áp thực tế
```

**Hiệu chuẩn cảm biến:** cho một lượng nước đã biết (vd. 10 lít) chảy qua, đọc
`total` báo về, rồi điều chỉnh `FLOW_PULSES_PER_LITER = 450 * (total_báo / 10)`.

---

## 6. Build & Nạp

Yêu cầu: [PlatformIO](https://platformio.org/).

```bash
# Biên dịch
pio run

# Nạp qua ST-Link (mặc định)
pio run -t upload

# Xem log debug (UART2, 115200 baud)
pio device monitor
```

Nếu nạp qua UART bootloader thay vì ST-Link, đổi trong `platformio.ini`:

```ini
upload_protocol = serial
```

---

## 7. Hạn chế & hướng phát triển

- Bộ đệm offline nằm trong **RAM** → mất khi reset. Có thể nâng cấp lưu vào
  **Flash nội** (emulated EEPROM) hoặc thẻ nhớ để bền dữ liệu.
- Chưa có chế độ **ngủ tiết kiệm điện** (STOP mode) — cần thiết khi chạy pin lâu
  dài; xem thêm module đo dòng và duty-cycle của modem.
- Có thể bổ sung **TLS (MQTTS)** cho A7680C và **OTA** cập nhật firmware.
- Phát hiện "lỗi cảm biến" hiện là điểm móc (hook) — nên bổ sung logic phân biệt
  "không có nước" với "cảm biến hỏng" (vd. kiểm tra hở mạch tín hiệu).
