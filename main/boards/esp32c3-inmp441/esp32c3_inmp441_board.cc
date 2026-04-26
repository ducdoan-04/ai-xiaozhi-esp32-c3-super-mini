/**
 * ESP32-C3 Super Mini - DASAI XIAOZHI V0.01
 * 
 * Hardware Configuration:
 * ┌─────────────────────────────────────────────────────────────┐
 * │  MAX98357A (Loa / Speaker)                                  │
 * │    SD+Vin --> 5V  |  Gain+Gnd --> GND                       │
 * │    Din    --> GPIO 6  (I2S Data Out)                        │
 * │    Bclk   --> GPIO 7  (I2S Bit Clock - Speaker)             │
 * │    Lrc    --> GPIO 3  (I2S Word Select - Speaker)           │
 * ├─────────────────────────────────────────────────────────────┤
 * │  INMP441 (Microphone)                                       │
 * │    Gnd+L/R --> GND  |  Vdd --> 3.3V                        │
 * │    Sd  --> GPIO 10 (I2S Data In from Mic)                   │
 * │    Ws  --> GPIO 2  (I2S Word Select - Mic)                  │
 * │    Sck --> GPIO 1  (I2S Bit Clock - Mic)                    │
 * ├─────────────────────────────────────────────────────────────┤
 * │  OLED SSD1306 0.96"                                         │
 * │    SCL --> GPIO 5  |  SDA --> GPIO 4                        │
 * ├─────────────────────────────────────────────────────────────┤
 * │  TTP223-B (Touch Push-to-Talk) --> GPIO 0 (active HIGH)     │
 * │  TTP223-A (Touch Mode/Volume)  --> GPIO 9 (active HIGH)     │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * NOTE: Since MAX98357A and INMP441 have DIFFERENT BCLK/WS pins,
 *       we use NoAudioCodecSimplex (2 separate I2S peripherals):
 *       - I2S Port 0 (TX): MAX98357A speaker output
 *       - I2S Port 1 (RX): INMP441 microphone input
 */

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "protocol.h"

#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_ssd1306.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>

#define TAG "Esp32c3SuperMiniBoard"

class Esp32c3Inmp441Board : public WifiBoard {
private:
    Button touch_button_b_;    // TTP223-B: Push-to-talk (GPIO 0)
    Button touch_button_a_;    // TTP223-A: Mode/Volume  (GPIO 9)
    i2c_master_bus_handle_t i2c_bus_ = nullptr;

    // ─── I2C Initialization for OLED ──────────────────────────
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = true,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C initialized: SDA=GPIO%d, SCL=GPIO%d",
                 (int)DISPLAY_SDA_PIN, (int)DISPLAY_SCL_PIN);
    }

    // ─── Button / Touch Initialization ────────────────────────
    void InitializeButtons() {
        // TTP223-B (GPIO 0): Push-to-Talk (active HIGH: HIGH=touched)
        touch_button_b_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        touch_button_b_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        // TTP223-A (GPIO 9): Dasai Mochi face  (active_high=true)
        // ⚠ TTP223-A output: HIGH khi idle, LOW khi chạm → INVERTED vs normal
        //   Với active_high=true:
        //     PressUp   = GPIO LOW  = người dùng ĐANG CHẠM  → Hiện mặt
        //     PressDown = GPIO HIGH = người dùng VỪA THẢ    → Ẩn mặt
        touch_button_a_.OnPressUp([this]() {
            ESP_LOGI(TAG, "TTP223-A: chạm → Dasai Mochi ON");
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                display->ShowDasaiFace(true);
            }
        });
        touch_button_a_.OnPressDown([this]() {
            ESP_LOGI(TAG, "TTP223-A: thả → Dasai Mochi OFF");
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                display->ShowDasaiFace(false);
            }
        });

        // Double-click TTP223-A: ngắt AI đang nói
        touch_button_a_.OnDoubleClick([this]() {
            ESP_LOGI(TAG, "TTP223-A: double-click → abort AI");
            Application::GetInstance().AbortSpeaking(kAbortReasonNone);
        });
    }

public:
    Esp32c3Inmp441Board()
        : touch_button_b_(TOUCH_BUTTON_B_GPIO, /*active_high=*/true),
          touch_button_a_(TOUCH_BUTTON_A_GPIO, /*active_high=*/true)
    {
        ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║  DASAI XIAOZHI V0.01 - C3 SUPER MINI    ║");
        ESP_LOGI(TAG, "╠══════════════════════════════════════════╣");
        ESP_LOGI(TAG, "║  Speaker (MAX98357A):                    ║");
        ESP_LOGI(TAG, "║    BCLK  = GPIO %-2d                       ║", (int)AUDIO_I2S_SPK_GPIO_BCLK);
        ESP_LOGI(TAG, "║    LRC   = GPIO %-2d                       ║", (int)AUDIO_I2S_SPK_GPIO_LRC);
        ESP_LOGI(TAG, "║    DIN   = GPIO %-2d                       ║", (int)AUDIO_I2S_SPK_GPIO_DOUT);
        ESP_LOGI(TAG, "║  Microphone (INMP441):                   ║");
        ESP_LOGI(TAG, "║    SCK   = GPIO %-2d                       ║", (int)AUDIO_I2S_MIC_GPIO_SCK);
        ESP_LOGI(TAG, "║    WS    = GPIO %-2d                       ║", (int)AUDIO_I2S_MIC_GPIO_WS);
        ESP_LOGI(TAG, "║    SD    = GPIO %-2d                       ║", (int)AUDIO_I2S_MIC_GPIO_SD);
        ESP_LOGI(TAG, "║  OLED SSD1306:                           ║");
        ESP_LOGI(TAG, "║    SDA   = GPIO %-2d                       ║", (int)DISPLAY_SDA_PIN);
        ESP_LOGI(TAG, "║    SCL   = GPIO %-2d                       ║", (int)DISPLAY_SCL_PIN);
        ESP_LOGI(TAG, "║  Touch Buttons (TTP223):                 ║");
        ESP_LOGI(TAG, "║    B (PTT)  = GPIO %-2d                    ║", (int)TOUCH_BUTTON_B_GPIO);
        ESP_LOGI(TAG, "║    A (Mode) = GPIO %-2d                    ║", (int)TOUCH_BUTTON_A_GPIO);
        ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

        InitializeI2c();
        InitializeButtons();
    }

    // ─── Audio Codec ───────────────────────────────────────────
    // Uses NoAudioCodecSimplex because:
    //   - Speaker (MAX98357A): BCLK=GPIO7, LRC=GPIO3, DIN=GPIO6
    //   - Microphone (INMP441): SCK=GPIO1,  WS=GPIO2,  SD=GPIO10
    //   These use DIFFERENT clock pins → 2 I2S peripherals needed
    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,       // 16000 Hz (mic)
            AUDIO_OUTPUT_SAMPLE_RATE,      // 24000 Hz (speaker)
            AUDIO_I2S_SPK_GPIO_BCLK,       // Speaker BCLK  = GPIO 7
            AUDIO_I2S_SPK_GPIO_LRC,        // Speaker LRC   = GPIO 3
            AUDIO_I2S_SPK_GPIO_DOUT,       // Speaker DIN   = GPIO 6
            AUDIO_I2S_MIC_GPIO_SCK,        // Mic SCK       = GPIO 1
            AUDIO_I2S_MIC_GPIO_WS,         // Mic WS        = GPIO 2
            AUDIO_I2S_MIC_GPIO_SD          // Mic SD        = GPIO 10
        );
        return &audio_codec;
    }

    // ─── OLED Display ──────────────────────────────────────────
    virtual Display* GetDisplay() override {
        static Display* display = nullptr;

        if (display == nullptr) {
            // Try SSD1306 at 0x3C first, then 0x3D
            const uint8_t i2c_addresses[] = {0x3C, 0x3D};
            esp_lcd_panel_io_handle_t panel_io = nullptr;
            esp_lcd_panel_handle_t panel = nullptr;

            for (uint8_t addr : i2c_addresses) {
                ESP_LOGI(TAG, "Trying OLED SSD1306 at I2C 0x%02X...", addr);

                esp_lcd_panel_io_i2c_config_t io_config = {
                    .dev_addr = addr,
                    .on_color_trans_done = nullptr,
                    .user_ctx = nullptr,
                    .control_phase_bytes = 1,
                    .dc_bit_offset = 6,
                    .lcd_cmd_bits = 8,
                    .lcd_param_bits = 8,
                    .flags = {
                        .dc_low_on_data = false,
                        .disable_control_phase = false,
                    },
                    .scl_speed_hz = 400000,  // 400kHz Fast Mode
                };

                if (esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &panel_io) != ESP_OK) {
                    ESP_LOGW(TAG, "  Failed to create panel IO at 0x%02X", addr);
                    continue;
                }

                esp_lcd_panel_dev_config_t panel_config = {
                    .reset_gpio_num = GPIO_NUM_NC,
                    .bits_per_pixel = 1,
                    .flags = {
                        .reset_active_high = false,
                    },
                    .vendor_config = nullptr,
                };

                if (esp_lcd_new_panel_ssd1306(panel_io, &panel_config, &panel) != ESP_OK) {
                    ESP_LOGW(TAG, "  Failed to create SSD1306 panel at 0x%02X", addr);
                    esp_lcd_panel_io_del(panel_io);
                    panel_io = nullptr;
                    continue;
                }

                if (esp_lcd_panel_reset(panel) != ESP_OK ||
                    esp_lcd_panel_init(panel) != ESP_OK ||
                    esp_lcd_panel_disp_on_off(panel, true) != ESP_OK)
                {
                    ESP_LOGW(TAG, "  Failed to init panel at 0x%02X", addr);
                    esp_lcd_panel_del(panel);
                    esp_lcd_panel_io_del(panel_io);
                    panel = nullptr;
                    panel_io = nullptr;
                    continue;
                }

                // ✓ OLED found and initialized
                ESP_LOGI(TAG, "  OLED SSD1306 OK at 0x%02X (%dx%d)", addr, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                display = new OledDisplay(panel_io, panel,
                                          DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                          DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
                break;
            }

            if (display == nullptr) {
                ESP_LOGW(TAG, "OLED not found - running without display");
                static NoDisplay no_display;
                display = &no_display;
            }
        }
        return display;
    }
};

DECLARE_BOARD(Esp32c3Inmp441Board);
