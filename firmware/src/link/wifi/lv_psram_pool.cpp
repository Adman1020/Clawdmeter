#include "link/wifi/lv_psram_pool.h"
#include <esp_heap_caps.h>

void* lv_psram_pool_alloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
