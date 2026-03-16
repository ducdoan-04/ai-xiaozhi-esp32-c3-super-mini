# ESP32-C3 Super Mini - DASAI XIAOZHI V0.01

Mạch AI giọng nói Xiaozhi dành cho **ESP32-C3 Super Mini** với:
- 🎤 **Microphone**: INMP441 (I2S digital mic)
- 🔊 **Loa / Speaker**: MAX98357A (I2S Class-D amplifier)
- 🖥️ **Màn hình**: SSD1306 OLED 0.96" 128×64 (I2C)
- 👆 **Nút cảm ứng B** (TTP223-B): Push-to-Talk
- 👆 **Nút cảm ứng A** (TTP223-A): Mode / Bỏ câu trả lời

---

## Sơ đồ kết nối (Wiring)

### MAX98357A (Loa / Speaker)

| MAX98357A | ESP32-C3 Super Mini | Ghi chú |
|-----------|-------------------|---------|
| SD + Vin  | 5V                | Power   |
| Gain + Gnd | GND              | Gain = 9dB khi để trống |
| Din       | **GPIO 6**        | I2S Data Out |
| Bclk      | **GPIO 7**        | I2S Bit Clock (Speaker) |
| Lrc       | **GPIO 3**        | I2S Word Select (Speaker) |

### INMP441 (Microphone)

| INMP441   | ESP32-C3 Super Mini | Ghi chú |
|-----------|-------------------|---------|
| Gnd + L/R | GND               | L/R = GND → Kênh trái |
| Vdd       | 3.3V              | Power |
| Sd        | **GPIO 10**       | I2S Data In (Mic) |
| Ws        | **GPIO 2**        | I2S Word Select (Mic) |
| Sck       | **GPIO 1**        | I2S Bit Clock (Mic) |

### OLED SSD1306 0.96" (128×64)

| OLED   | ESP32-C3 Super Mini | Ghi chú |
|--------|-------------------|---------|
| Vcc    | 3.3V              | Power |
| Gnd    | GND               | Ground |
| SCL    | **GPIO 5**        | I2C Clock |
| SDA    | **GPIO 4**        | I2C Data |

### TTP223-B (Nút Push-to-Talk)

| TTP223-B | ESP32-C3 Super Mini | Ghi chú |
|----------|-------------------|---------|
| Vcc      | 3.3V              | Power |
| Gnd      | GND               | Ground |
| I/O      | **GPIO 0**        | Cảm ứng → HIGH |

### TTP223-A (Nút Mode / Bỏ câu trả lời)

| TTP223-A | ESP32-C3 Super Mini | Ghi chú |
|----------|-------------------|---------|
| Vcc      | 3.3V              | Power |
| I/O      | **GPIO 9**        | Cảm ứng → HIGH |
| Gnd      | GND               | Ground |

---

## Nguyên lý hoạt động Audio

ESP32-C3 có **2 I2S peripheral** → Dùng **Simplex mode** (2 bus riêng biệt):
- **I2S Port 0 (TX)**: Phát âm thanh → MAX98357A (BCLK=7, LRC=3, DIN=6)
- **I2S Port 1 (RX)**: Thu âm thanh ← INMP441 (SCK=1, WS=2, SD=10)

---

## Hướng dẫn sử dụng

### Lần đầu khởi động (Cấu hình WiFi)
1. Bật nguồn → thiết bị vào chế độ AP
2. Kết nối điện thoại vào WiFi `Xiaozhi-XXXXXX`
3. Mở trình duyệt → vào `192.168.4.1`
4. Nhập tên WiFi + mật khẩu → Lưu
5. Thiết bị tự động kết nối và hiển thị mã kích hoạt trên OLED

### Sử dụng hàng ngày
| Thao tác | Kết quả |
|----------|---------|
| **Chạm giữ TTP223-B** | Bắt đầu ghi âm (Push-to-Talk) |
| **Thả TTP223-B**      | Dừng ghi, gửi lên máy chủ AI |
| **Click TTP223-B** (khi đang khởi động) | Vào chế độ cấu hình WiFi |
| **Giữ TTP223-B** (5 giây) | Xóa WiFi, cấu hình lại |
| **Double-click TTP223-A** | Bỏ câu trả lời hiện tại |

### Compile & Flash
```bash
# Thiết lập target
idf.py set-target esp32c3

# Cấu hình (chọn Board Type = ESP32-C3 INMP441+MAX98357A)
idf.py menuconfig

# Build
idf.py build

# Flash và monitor
idf.py -p COM3 flash monitor
```

---

## Lưu ý phần cứng

- **TTP223 Jump**: Mặc định TTP223 là chế độ **Toggle** (lật trạng thái mỗi lần chạm). Nếu muốn **Momentary** (chỉ HIGH khi chạm), cần hàn Bridge A trên module TTP223.
- **MAX98357A Gain**: Khi chân GAIN nối GND → độ khuếch đại 9dB (mặc định). Để trống → 12dB.
- **INMP441 L/R**: Kết nối chân L/R xuống GND để chọn kênh **trái** (Left channel).
- **Nguồn**: MAX98357A cần 5V cho công suất đủ; các IC còn lại dùng 3.3V.
