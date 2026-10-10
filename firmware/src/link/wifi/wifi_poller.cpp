// WiFi poller: joins the user's network, syncs time, and every 60 s POSTs a
// 1-token Haiku request purely to read the anthropic-ratelimit-unified-*
// response headers, which carry the same utilization data the host daemon
// forwards over BLE. Runs as its own FreeRTOS task so TLS handshakes never
// stall the LVGL loop.
#include "link/wifi/wifi_poller.h"
#include "link/wifi/provisioning.h"
#include "link/wifi/captive_portal.h"
#include "link/wifi/usage_payload.h"
#include "link/wifi/gts_roots.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <time.h>
#include <string.h>

static const char* API_URL   = "https://api.anthropic.com/v1/messages";
static const char* API_BODY  = "{\"model\":\"claude-haiku-4-5-20251001\",\"max_tokens\":1,\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}";
static const uint32_t POLL_MS_MIN = 60000, POLL_MS_MAX = 300000;
static const int      CONNECT_FAILS_TO_PORTAL = 3;
static const time_t   SANE_EPOCH = 1704067200;   // 2024-01-01

// Order matches RateLimitHeaders field order in usage_payload.h.
static const char* HDRS[] = {
    "anthropic-ratelimit-unified-5h-utilization",
    "anthropic-ratelimit-unified-5h-reset",
    "anthropic-ratelimit-unified-5h-status",
    "anthropic-ratelimit-unified-7d-utilization",
    "anthropic-ratelimit-unified-7d-reset",
    "anthropic-ratelimit-unified-status",
    "anthropic-ratelimit-unified-overage-utilization",
    "anthropic-ratelimit-unified-overage-reset",
};
static const size_t NHDRS = sizeof HDRS / sizeof HDRS[0];

static WifiConfig                  s_cfg;
static volatile poller_phase_t     s_phase = POLLER_IDLE;
static volatile bool               s_portal_req = false;
static volatile bool               s_success = false;
static volatile int                s_conn_fails = 0;
static const char* volatile        s_error = "";
static SemaphoreHandle_t           s_mutex;
static char                        s_payload[320];
static volatile bool               s_payload_ready = false;

// Route big mbedTLS buffers (TLS record buffers are 16 KB each) to PSRAM so
// NimBLE and the TLS session can coexist in internal SRAM.
static void* tls_calloc(size_t n, size_t sz) {
    void* p = nullptr;
    if (n * sz >= 512) p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)            p = heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

static void publish(const char* json) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_payload, json, sizeof s_payload - 1);
    s_payload[sizeof s_payload - 1] = '\0';
    s_payload_ready = true;
    xSemaphoreGive(s_mutex);
}

// One API round-trip. Returns true when a usable payload was published.
static bool poll_once() {
    WiFiClientSecure tls;
    tls.setCACert(GTS_ROOT_R4_CA);
    tls.setTimeout(15);
    HTTPClient http;
    http.setTimeout(15000);
    http.setReuse(false);
    if (!http.begin(tls, API_URL)) { s_error = "TLS error"; return false; }
    http.addHeader("Authorization", String("Bearer ") + s_cfg.token);
    http.addHeader("anthropic-version", "2023-06-01");
    http.addHeader("anthropic-beta", "oauth-2025-04-20");
    http.addHeader("Content-Type", "application/json");
    http.setUserAgent("Clawdmeter-wifi/1.0");
    http.collectHeaders(HDRS, NHDRS);

    int code = http.POST((uint8_t*)API_BODY, strlen(API_BODY));
    if (code <= 0) { s_error = "No network"; http.end(); return false; }

    String v[NHDRS];
    for (size_t i = 0; i < NHDRS; i++) v[i] = http.header(HDRS[i]);
    http.end();

    // Rate-limit headers arrive on 200 *and* on 429 (limit exceeded); use both.
    if (code == 401) { s_error = "Bad token (401)"; return false; }
    if (code != 200 && code != 429) {
        static char e[24];
        snprintf(e, sizeof e, "API error %d", code);
        s_error = e;
        return false;
    }

    RateLimitHeaders h = { v[0].c_str(), v[1].c_str(), v[2].c_str(), v[3].c_str(),
                           v[4].c_str(), v[5].c_str(), v[6].c_str(), v[7].c_str() };
    PayloadOptions o = { (long)time(nullptr), s_cfg.tz_minutes, s_cfg.hour_fmt };
    char out[320];
    if (!usage_payload_build(h, o, out, sizeof out)) { s_error = "No usage headers"; return false; }
    publish(out);
    s_error = (code == 429) ? "Rate limited (429)" : "";
    Serial.printf("wifi: poll ok (%d) heap=%u psram=%u\n", code,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
}

// Never returns: POST /save in the portal restarts the device.
static void enter_portal() {
    s_phase = POLLER_PORTAL;
    if (WiFi.getMode() & WIFI_MODE_STA) WiFi.disconnect(false);
    portal_start(s_cfg);
    for (;;) { portal_tick(); vTaskDelay(pdMS_TO_TICKS(5)); }
}

static void task(void*) {
    if (!prov_has_wifi(s_cfg)) enter_portal();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname("clawdmeter");
    uint32_t interval = POLL_MS_MIN;

    for (;;) {
        if (s_portal_req) enter_portal();

        if (WiFi.status() != WL_CONNECTED) {
            s_phase = POLLER_CONNECTING;
            WiFi.begin(s_cfg.ssid, s_cfg.pass);
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000 && !s_portal_req)
                vTaskDelay(pdMS_TO_TICKS(100));
            if (WiFi.status() != WL_CONNECTED) {
                s_error = "No network";
                // Wrong password / vanished network on a never-connected device →
                // reopen the portal. After a successful connect, just keep retrying.
                s_conn_fails = s_conn_fails + 1;
                if (s_conn_fails >= CONNECT_FAILS_TO_PORTAL && !s_success) enter_portal();
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            s_conn_fails = 0;
            Serial.printf("wifi: connected to %s ip=%s\n", s_cfg.ssid, WiFi.localIP().toString().c_str());
            configTime(0, 0, "pool.ntp.org", "time.google.com");
        }

        if (time(nullptr) < SANE_EPOCH) {
            s_phase = POLLER_WAIT_TIME;
            s_error = "Waiting for time";
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        s_phase = POLLER_POLLING;
        if (!prov_has_token(s_cfg)) {
            s_error = "No token - hold PWR to set up";
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (poll_once()) { s_success = true; interval = POLL_MS_MIN; }
        else {
            interval = interval * 2 > POLL_MS_MAX ? POLL_MS_MAX : interval * 2;
            Serial.printf("wifi: poll failed (%s), next in %lus\n", s_error, (unsigned long)(interval / 1000));
        }

        for (uint32_t waited = 0; waited < interval && !s_portal_req; waited += 250)
            vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void poller_start(const WifiConfig& cfg) {
    s_cfg = cfg;
    s_mutex = xSemaphoreCreateMutex();
    mbedtls_platform_set_calloc_free(tls_calloc, free);
    // Stack stays in internal SRAM: WiFi init and NVS writes run flash
    // operations with the cache disabled, which a PSRAM-resident stack cannot
    // survive (the task silently stalled on hardware with a PSRAM stack). The
    // big TLS buffers still land in PSRAM via tls_calloc above.
    BaseType_t ok = xTaskCreatePinnedToCore(task, "wifi_poll", 8192, nullptr, 1, nullptr, 0);
    if (ok != pdPASS) { s_error = "Task start failed"; Serial.println("wifi: poller task create failed"); }
}

void poller_request_portal(void)    { s_portal_req = true; }
poller_phase_t poller_phase(void)   { return s_phase; }
bool  poller_had_success(void)      { return s_success; }
const char* poller_error(void)      { return s_error; }
int   poller_connect_failures(void) { return s_conn_fails; }
bool  poller_has_payload(void)      { return s_payload_ready; }

bool poller_take_payload(char* dst, size_t cap) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool had = s_payload_ready;
    if (had) { strncpy(dst, s_payload, cap - 1); dst[cap - 1] = '\0'; s_payload_ready = false; }
    xSemaphoreGive(s_mutex);
    return had;
}
