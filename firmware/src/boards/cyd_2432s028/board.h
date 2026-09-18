#pragma once

// ESP32-2432S028 — the "Cheap Yellow Display" (CYD).
// 2.8" 320x240 ILI9341 over plain 4-wire SPI + XPT2046 resistive touch on a
// second, physically separate SPI bus. No PMU, no IMU, no battery circuit,
// no IO expander, no audio path — USB powered, one usable button.
//
// First port in the tree to a classic ESP32 (ESP32-D0WD-V3, dual-core Xtensa,
// 4 MB flash, no PSRAM — verified with `esptool flash_id` on the unit this
// port was developed against). That means no native USB: Serial is UART0 over
// the onboard CP2102 bridge, so flash/monitor over /dev/cu.usbserial-* rather
// than the /dev/cu.usbmodem* path the S3 and C6 ports use.
//
// Some 2432S028 batches ship an ST7789 panel or a capacitive CST820 touch
// controller instead. This port targets the ILI9341 + XPT2046 variant.

#define BOARD_NAME           "ESP32-2432S028 (CYD)"

// ---- Display geometry ----
// The ILI9341 is 240x320 native; the driver's rotation parameter performs a
// true MADCTL row/column exchange (unlike the CO5300 panels, which need CPU
// pixel remapping), so landscape costs nothing at runtime.
#define LCD_WIDTH            320
#define LCD_HEIGHT           240
#define LCD_ROTATION         1     // 1 = landscape, USB socket on the right

// ---- Display SPI (ILI9341, 4-wire) ----
// The panel's reset is tied to the module reset, not broken out to a GPIO.
#define LCD_CS               15
#define LCD_SCLK             14
#define LCD_MOSI             13
#define LCD_MISO             12
#define LCD_DC               2
#define LCD_BL               21    // backlight, LEDC PWM (ILI9341 has no brightness cmd)

// ---- Touch (XPT2046 resistive, bit-banged SPI) ----
// Separate pins from the display, so there is no bus contention. Bit-banging
// keeps the port free of both a touch library and any question about which
// hardware SPI peripheral the GFX library claimed for the panel — the XPT2046
// tops out around 2 MHz anyway, well inside what digitalWrite can drive.
#define TP_CLK               25
#define TP_CS                33
#define TP_MOSI              32
#define TP_MISO              39
#define TP_IRQ               36    // PENIRQ: LOW while the panel is pressed

// Resistive-touch calibration. The XPT2046 reports raw 12-bit ADC counts, not
// pixels, and the usable range stops well short of the full 0..4095 because of
// the panel's edge resistance. The values below were measured on hardware by
// holding each screen corner in turn and logging the raw pairs; they vary
// between physical units, so retune here if taps land offset.
//
// The panel's axes are transposed relative to the landscape display: raw Y
// tracks screen X (345 at the left edge, 3647 at the right) and raw X tracks
// screen Y (550 at the top, 3646 at the bottom). Neither axis is reversed.
#define TP_RAW_X_MIN         565   // raw X at the top edge    → screen Y = 0
#define TP_RAW_X_MAX         3625  // raw X at the bottom edge → screen Y = max
#define TP_RAW_Y_MIN         320   // raw Y at the left edge   → screen X = 0
#define TP_RAW_Y_MAX         3610  // raw Y at the right edge  → screen X = max
#define TP_SWAP_XY           1
#define TP_INVERT_X          0
#define TP_INVERT_Y          0

// ---- Buttons ----
// BOOT is the only usable user button; the other onboard button is EN/reset.
#define BTN_BACK_GPIO        0     // BOOT — primary, Space (PTT)

// ---- Capability flags ----
#define BOARD_HAS_SECONDARY_BUTTON 0
#define BOARD_HAS_ROTATION         0
#define BOARD_HAS_IMU              0
#define BOARD_HAS_BATTERY          0
#define BOARD_HAS_IO_EXPANDER      0
#define BOARD_HAS_SOUND            0
