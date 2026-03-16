#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s_std.h>

// =============================================================
// ESP32-C3 Super Mini - DASAI XIAOZHI V0.01
// Board: C3 SUPER MINI
// =============================================================
//
// PIN MAPPING:
// ┌─────────────────────────────────────────────────────────┐
// │  MAX98357A (Loa / Speaker)                              │
// │    SD+Vin  --> 5V                                       │
// │    Gain+Gnd --> GND                                     │
// │    Din     --> GPIO 6  (I2S Data Out)                   │
// │    Bclk    --> GPIO 7  (I2S Bit Clock - SPK)            │
// │    Lrc     --> GPIO 3  (I2S Word Select - SPK)          │
// ├─────────────────────────────────────────────────────────┤
// │  INMP441 (Microphone)                                   │
// │    Gnd+L/R --> GND                                      │
// │    Vdd     --> 3.3V                                     │
// │    Sd      --> GPIO 10 (I2S Data In)                    │
// │    Ws      --> GPIO 2  (I2S Word Select - MIC)          │
// │    Sck     --> GPIO 1  (I2S Bit Clock - MIC)            │
// ├─────────────────────────────────────────────────────────┤
// │  OLED SSD1306 0.96"                                     │
// │    Vcc     --> 3.3V                                     │
// │    Gnd     --> GND                                      │
// │    SCL     --> GPIO 5  (I2C Clock)                      │
// │    SDA     --> GPIO 4  (I2C Data)                       │
// ├─────────────────────────────────────────────────────────┤
// │  TTP223-B (Touch Button - Push to Talk)                 │
// │    Vcc     --> 3.3V                                     │
// │    Gnd     --> GND                                      │
// │    I/O     --> GPIO 0                                   │
// ├─────────────────────────────────────────────────────────┤
// │  TTP223-A (Touch Button - Volume/Mode)                  │
// │    Vcc     --> 3.3V                                     │
// │    I/O     --> GPIO 9                                   │
// │    Gnd     --> GND                                      │
// └─────────────────────────────────────────────────────────┘

// =============================================================
// Audio Sample Rates
// =============================================================
#define AUDIO_INPUT_SAMPLE_RATE  16000   // INMP441 mic: 16kHz
#define AUDIO_OUTPUT_SAMPLE_RATE 24000   // MAX98357A speaker: 24kHz

// =============================================================
// MAX98357A I2S Speaker (I2S Port 0 - TX only)
// =============================================================
#define AUDIO_I2S_SPK_GPIO_BCLK  GPIO_NUM_7   // MAX98357A Bclk
#define AUDIO_I2S_SPK_GPIO_LRC   GPIO_NUM_3   // MAX98357A Lrc (WS)
#define AUDIO_I2S_SPK_GPIO_DOUT  GPIO_NUM_6   // MAX98357A Din (Data Out from ESP)

// =============================================================
// INMP441 I2S Microphone (I2S Port 1 - RX only)
// =============================================================
#define AUDIO_I2S_MIC_GPIO_SCK   GPIO_NUM_1   // INMP441 Sck (Bit Clock)
#define AUDIO_I2S_MIC_GPIO_WS    GPIO_NUM_2   // INMP441 Ws  (Word Select)
#define AUDIO_I2S_MIC_GPIO_SD    GPIO_NUM_10  // INMP441 Sd  (Data In to ESP)

// =============================================================
// OLED SSD1306 I2C Display
// =============================================================
#define DISPLAY_SDA_PIN     GPIO_NUM_4   // SDA
#define DISPLAY_SCL_PIN     GPIO_NUM_5   // SCL
#define DISPLAY_WIDTH       128
#define DISPLAY_HEIGHT      64
#define DISPLAY_MIRROR_X    true
#define DISPLAY_MIRROR_Y    true

// =============================================================
// Touch Buttons (TTP223 capacitive touch)
// =============================================================
#define TOUCH_BUTTON_B_GPIO GPIO_NUM_0   // TTP223-B: Push-to-Talk (active HIGH)
#define TOUCH_BUTTON_A_GPIO GPIO_NUM_9   // TTP223-A: Volume/Mode  (active HIGH)

// =============================================================
// LED (not present on C3 Super Mini)
// =============================================================
#define BUILTIN_LED_GPIO    GPIO_NUM_NC  // No built-in LED

#endif // _BOARD_CONFIG_H_
