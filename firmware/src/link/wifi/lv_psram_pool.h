// LVGL's builtin allocator normally keeps its LV_MEM_SIZE pool as a static
// array in internal SRAM. The WiFi env cannot afford that (WiFi + NimBLE + TLS
// exhausted internal RAM on hardware: "esp-aes: Failed to allocate memory"),
// so the env sets LV_MEM_POOL_INCLUDE/LV_MEM_POOL_ALLOC to carve it from PSRAM.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* lv_psram_pool_alloc(size_t size);
#ifdef __cplusplus
}
#endif
