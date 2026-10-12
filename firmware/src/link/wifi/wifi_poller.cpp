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
static const uint32_t CONNECT_TIMEOUT_MS = 20000;
static const uint32_t RECONNECT_MS_MIN = 5000, RECONNECT_MS_MAX = 60000;
static const int      CONNECT_FAILS_TO_PORTAL = 3;
static const uint32_t PORTAL_IDLE_EXIT_MS = 5 * 60 * 1000;   // unattended hotspot closes after this
static const time_t   SANE_EPOCH = 1704067200;               // 2024-01-01
static const uint32_t TASK_STACK_BYTES = 12288;

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

static WifiConfig               s_cfg;
static volatile uint32_t        s_gen = 1;
static volatile poller_phase_t  s_phase = POLLER_IDLE;
static volatile poller_err_t    s_err = PERR_NONE;
static volatile int             s_err_code = 0;
static volatile bool            s_portal_req = false;
static volatile bool            s_success = false;
static volatile bool            s_last_ok = false;
static SemaphoreHandle_t        s_mutex;
static char                     s_payload[320];
static volatile bool            s_payload_ready = false;

static void set_phase(poller_phase_t ph) { if (s_phase != ph) { s_phase = ph; s_gen = s_gen + 1; } }
static void set_err(poller_err_t e, int code = 0) {
    if (s_err != e || s_err_code != code) { s_err = e; s_err_code = code; s_gen = s_gen + 1; }
}

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
    tls.setHandshakeTimeout(15);            // seconds; default 120 s would park the task
    HTTPClient http;
    http.setTimeout(15000);
    http.setReuse(false);
    if (!http.begin(tls, API_URL)) { set_err(PERR_TLS); return false; }
    http.addHeader("Authorization", String("Bearer ") + s_cfg.token);
    http.addHeader("anthropic-version", "2023-06-01");
    http.addHeader("anthropic-beta", "oauth-2025-04-20");
    http.addHeader("Content-Type", "application/json");
    http.setUserAgent("Clawdmeter-wifi/1.0");
    http.collectHeaders(HDRS, NHDRS);

    int code = http.POST((uint8_t*)API_BODY, strlen(API_BODY));
    if (code <= 0) { http.end(); set_err(code == HTTPC_ERROR_CONNECTION_REFUSED || code == HTTPC_ERROR_CONNECTION_LOST ? PERR_NO_NETWORK : PERR_TLS, code); return false; }

    String v[NHDRS];
    for (size_t i = 0; i < NHDRS; i++) v[i] = http.header(HDRS[i]);   // must read before end()
    http.end();

    // Rate-limit headers arrive on 200 *and* on 429 (limit exceeded); use both.
    if (code == 401) { set_err(PERR_BAD_TOKEN, code); return false; }
    if (code != 200 && code != 429) { set_err(PERR_API, code); return false; }

    RateLimitHeaders h = { v[0].c_str(), v[1].c_str(), v[2].c_str(), v[3].c_str(),
                           v[4].c_str(), v[5].c_str(), v[6].c_str(), v[7].c_str() };
    PayloadOptions o = { (long)time(nullptr), s_cfg.tz_minutes, s_cfg.hour_fmt };
    char out[320];
    if (!usage_payload_build(h, o, out, sizeof out)) { set_err(PERR_NO_HEADERS, code); return false; }
    publish(out);
    set_err(code == 429 ? PERR_RATE_LIMITED : PERR_NONE, code);
    Serial.printf("wifi: poll ok (%d) heap=%u psram=%u stack_free=%u\n", code,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t));
    return true;
}

// Run the hotspot until the form is saved (device restarts), the user presses
// the PWR gesture again, or it sits unattended for PORTAL_IDLE_EXIT_MS while
// WiFi credentials exist. The timeout matters after a power cut: the board
// boots before the router does, and must not stay an open AP forever.
static void run_portal() {
    set_phase(POLLER_PORTAL);
    if (WiFi.getMode() & WIFI_MODE_STA) WiFi.disconnect(false);
    portal_start(s_cfg);
    vTaskDelay(pdMS_TO_TICKS(1500));   // let setup()'s own prints finish — USB-CDC writes from two cores collide
    Serial.printf("portal: hotspot \"Clawdmeter\" up at %s\n", WiFi.softAPIP().toString().c_str());
    s_portal_req = false;
    uint32_t idle_since = millis();
    for (;;) {
        portal_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
        if (WiFi.softAPgetStationNum() > 0) idle_since = millis();   // someone is on it; stay
        bool gesture = s_portal_req;
        bool timed_out = prov_has_wifi(s_cfg) && (millis() - idle_since > PORTAL_IDLE_EXIT_MS);
        if (gesture || timed_out) {
            Serial.printf("portal: closing (%s)\n", gesture ? "gesture" : "unattended");
            s_portal_req = false;
            portal_stop();
            WiFi.mode(WIFI_STA);
            return;
        }
    }
}

static void task(void*) {
    if (!prov_has_wifi(s_cfg)) run_portal();   // first boot: nothing to connect to

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("clawdmeter");
    uint32_t poll_interval = POLL_MS_MIN;
    uint32_t reconnect_wait = RECONNECT_MS_MIN;
    int conn_fails = 0;

    for (;;) {
        if (s_portal_req) { run_portal(); conn_fails = 0; reconnect_wait = RECONNECT_MS_MIN; }

        if (WiFi.status() != WL_CONNECTED) {
            set_phase(POLLER_CONNECTING);
            WiFi.begin(s_cfg.ssid, s_cfg.pass);
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < CONNECT_TIMEOUT_MS && !s_portal_req)
                vTaskDelay(pdMS_TO_TICKS(100));
            if (WiFi.status() != WL_CONNECTED) {
                set_err(PERR_NO_NETWORK);
                conn_fails++;
                Serial.printf("wifi: connect failed (status %d, attempt %d)\n", (int)WiFi.status(), conn_fails);
                // Only credentials that have NEVER worked send us back to the
                // portal (wrong password / typo). Known-good credentials just
                // keep retrying — the router is probably rebooting.
                if (!s_cfg.ever_ok && conn_fails >= CONNECT_FAILS_TO_PORTAL) { run_portal(); conn_fails = 0; continue; }
                vTaskDelay(pdMS_TO_TICKS(reconnect_wait));
                reconnect_wait = reconnect_wait * 2 > RECONNECT_MS_MAX ? RECONNECT_MS_MAX : reconnect_wait * 2;
                continue;
            }
            conn_fails = 0;
            reconnect_wait = RECONNECT_MS_MIN;
            Serial.printf("wifi: connected to %s ip=%s\n", s_cfg.ssid, WiFi.localIP().toString().c_str());
            if (!s_cfg.ever_ok) { s_cfg.ever_ok = true; prov_save(s_cfg); }
            configTime(0, 0, "pool.ntp.org", "time.google.com");
        }

        if (time(nullptr) < SANE_EPOCH) {
            set_phase(POLLER_WAIT_TIME);
            set_err(PERR_WAIT_TIME);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        set_phase(POLLER_POLLING);
        if (!prov_has_token(s_cfg)) {
            set_err(PERR_NO_TOKEN);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        bool ok = poll_once();
        s_last_ok = ok;
        if (ok) { s_success = true; poll_interval = POLL_MS_MIN; }
        else {
            poll_interval = poll_interval * 2 > POLL_MS_MAX ? POLL_MS_MAX : poll_interval * 2;
            Serial.printf("wifi: poll failed (err %d code %d), next in %lus heap=%u largest=%u min=%u\n",
                          (int)s_err, (int)s_err_code, (unsigned long)(poll_interval / 1000),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        }
        s_gen = s_gen + 1;

        for (uint32_t waited = 0; waited < poll_interval && !s_portal_req; waited += 250)
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
    BaseType_t ok = xTaskCreatePinnedToCore(task, "wifi_poll", TASK_STACK_BYTES, nullptr, 1, nullptr, 0);
    if (ok != pdPASS) { set_err(PERR_TASK); Serial.println("wifi: poller task create failed"); }
}

void poller_request_portal(void)      { s_portal_req = true; }
uint32_t       poller_gen(void)       { return s_gen; }
poller_phase_t poller_phase(void)     { return s_phase; }
poller_err_t   poller_error(void)     { return s_err; }
int            poller_error_code(void){ return s_err_code; }
bool  poller_had_success(void)        { return s_success; }
bool  poller_last_poll_ok(void)       { return s_last_ok; }
bool  poller_has_payload(void)        { return s_payload_ready; }

bool poller_take_payload(char* dst, size_t cap) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool had = s_payload_ready;
    if (had) { strncpy(dst, s_payload, cap - 1); dst[cap - 1] = '\0'; s_payload_ready = false; }
    xSemaphoreGive(s_mutex);
    return had;
}
