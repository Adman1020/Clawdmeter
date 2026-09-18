#include "board.h"
#include <Arduino.h>

// Nothing to bring up before the display: no I2C devices, no IO expander
// gating reset lines, and the panel's reset is tied to the module reset.
// The touch pins are configured in touch_hal_init().
extern "C" void board_init(void) {
}
