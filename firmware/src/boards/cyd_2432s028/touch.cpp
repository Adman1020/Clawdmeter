#include "../../hal/touch_hal.h"
#include "board.h"
#include <Arduino.h>

// Minimal XPT2046 reader, bit-banged over the four dedicated touch pins.
//
// The first resistive panel in the tree. Unlike every capacitive controller
// here, the XPT2046 is just a 12-bit ADC across a resistive sheet: it reports
// raw counts, not pixels, so the readings need calibrating (bounds live in
// board.h) and filtering (the sheet is electrically noisy — a single sample
// routinely lands tens of counts away from its neighbours).
//
// Pressed-state comes from PENIRQ, which the controller pulls LOW while the
// sheet is shorted by a finger. It is polled rather than wired to an
// interrupt: the shared code already calls touch_hal_read() once per loop,
// and PENIRQ glitches during a conversion, which would make an edge ISR fire
// spuriously.

#define CMD_READ_X   0xD0   // A2..A0 = 101, 12-bit, PD = power down between
#define CMD_READ_Y   0x90   // A2..A0 = 001
#define SAMPLES      5      // odd, so the median is a real sample

static uint16_t last_x = 0;
static uint16_t last_y = 0;

static uint16_t xpt_transfer(uint8_t cmd) {
    digitalWrite(TP_CS, LOW);

    for (int i = 7; i >= 0; i--) {
        digitalWrite(TP_MOSI, (cmd >> i) & 1);
        digitalWrite(TP_CLK, HIGH);
        digitalWrite(TP_CLK, LOW);
    }

    // One busy clock while the converter settles, then 12 data bits MSB first.
    digitalWrite(TP_CLK, HIGH);
    digitalWrite(TP_CLK, LOW);

    uint16_t value = 0;
    for (int i = 0; i < 12; i++) {
        digitalWrite(TP_CLK, HIGH);
        value = (value << 1) | (digitalRead(TP_MISO) ? 1 : 0);
        digitalWrite(TP_CLK, LOW);
    }

    digitalWrite(TP_CS, HIGH);
    return value;
}

static uint16_t xpt_read_median(uint8_t cmd) {
    uint16_t s[SAMPLES];
    for (int i = 0; i < SAMPLES; i++) s[i] = xpt_transfer(cmd);

    for (int i = 1; i < SAMPLES; i++) {
        uint16_t v = s[i];
        int j = i - 1;
        while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
        s[j + 1] = v;
    }
    return s[SAMPLES / 2];
}

static uint16_t scale(uint16_t raw, uint16_t lo, uint16_t hi, int16_t span, bool invert) {
    if (raw < lo) raw = lo;
    if (raw > hi) raw = hi;
    int32_t p = (int32_t)(raw - lo) * (span - 1) / (hi - lo);
    if (invert) p = (span - 1) - p;
    return (uint16_t)p;
}

void touch_hal_init(void) {
    pinMode(TP_CS, OUTPUT);
    digitalWrite(TP_CS, HIGH);
    pinMode(TP_CLK, OUTPUT);
    digitalWrite(TP_CLK, LOW);
    pinMode(TP_MOSI, OUTPUT);
    pinMode(TP_MISO, INPUT);
    pinMode(TP_IRQ, INPUT);

    // A throwaway conversion leaves PENIRQ armed; without it the controller
    // can sit reporting "pressed" until the first real read.
    xpt_transfer(CMD_READ_X);
    Serial.printf("Touch XPT2046 ready (IRQ=%d)\n", digitalRead(TP_IRQ));
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    if (digitalRead(TP_IRQ) == LOW) {
        uint16_t raw_x = xpt_read_median(CMD_READ_X);
        uint16_t raw_y = xpt_read_median(CMD_READ_Y);

        // Re-check PENIRQ: a release landing mid-conversion yields a garbage
        // pair that would otherwise register as a tap in a random spot.
        if (digitalRead(TP_IRQ) == LOW) {
#if TP_SWAP_XY
            last_x = scale(raw_y, TP_RAW_Y_MIN, TP_RAW_Y_MAX, LCD_WIDTH, TP_INVERT_Y);
            last_y = scale(raw_x, TP_RAW_X_MIN, TP_RAW_X_MAX, LCD_HEIGHT, TP_INVERT_X);
#else
            last_x = scale(raw_x, TP_RAW_X_MIN, TP_RAW_X_MAX, LCD_WIDTH, TP_INVERT_X);
            last_y = scale(raw_y, TP_RAW_Y_MIN, TP_RAW_Y_MAX, LCD_HEIGHT, TP_INVERT_Y);
#endif
            *x = last_x;
            *y = last_y;
            *pressed = true;
            return;
        }
    }

    // LVGL wants the last known coordinate on release, not a jump to 0,0.
    *x = last_x;
    *y = last_y;
    *pressed = false;
}
