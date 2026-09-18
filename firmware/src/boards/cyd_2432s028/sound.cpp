#include "../../hal/sound_hal.h"

// The CYD has a small speaker header driven from GPIO 26, but no amplifier
// is populated and the pin is not wired up in this port. Session-reset chime
// is a no-op.

void sound_hal_init(void) {}
void sound_hal_tick(void) {}
void sound_hal_play_reset(void) {}
