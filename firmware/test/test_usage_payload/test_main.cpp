// Host unit test for usage_payload_build — the WiFi link's header→JSON step.
//   g++ -std=c++17 -I ../../src test_main.cpp ../../src/link/wifi/usage_payload.cpp -o t && ./t
#include "link/wifi/usage_payload.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define CHECK_CONTAINS(hay, needle) do { if (!strstr((hay), (needle))) { printf("FAIL %s:%d  %s not in %s\n", __FILE__, __LINE__, (needle), (hay)); ++failures; } } while (0)
#define CHECK_EQ_STR(got, want) do { if (strcmp((got), (want)) != 0) { printf("FAIL %s:%d\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, (got), (want)); ++failures; } } while (0)

static RateLimitHeaders pro(const char* u5, const char* r5, const char* u7, const char* r7, const char* st5 = "allowed") {
    RateLimitHeaders h = { u5, r5, st5, u7, r7, "allowed", "", "" };
    return h;
}
static RateLimitHeaders ent(const char* ou, const char* orst, const char* st = "allowed") {
    RateLimitHeaders h = { "", "", "", "", "", st, ou, orst };
    return h;
}

// 2026-10-10 12:00:00 UTC
static const long NOW = 1791633600L;

static void should_build_pro_payload_in_daemon_schema() {
    char out[256];
    char r5[16], r7[16];
    snprintf(r5, sizeof r5, "%ld", NOW + 90 * 60);
    snprintf(r7, sizeof r7, "%ld", NOW + 3 * 86400);
    PayloadOptions o = { NOW, 0, 0 };
    CHECK(usage_payload_build(pro("0.4251", r5, "0.12", r7), o, out, sizeof out));
    CHECK_EQ_STR(out, "{\"s\":43,\"sr\":90,\"w\":12,\"wr\":4320,\"st\":\"allowed\",\"acct\":\"pro\",\"c\":1,\"ok\":true}");
}

static void should_clamp_reset_in_past_to_zero() {
    char out[256];
    char r5[16]; snprintf(r5, sizeof r5, "%ld", NOW - 600);
    PayloadOptions o = { NOW, 0, 0 };
    CHECK(usage_payload_build(pro("0.9", r5, "0", "0"), o, out, sizeof out));
    CHECK_CONTAINS(out, "\"sr\":0,");
    CHECK_CONTAINS(out, "\"wr\":0,");
}

static void should_emit_clock_fields_when_hour_format_set() {
    char out[256];
    PayloadOptions o = { NOW, 60, 24 };   // UTC+1
    CHECK(usage_payload_build(pro("0", "0", "0", "0"), o, out, sizeof out));
    char expect[64]; snprintf(expect, sizeof expect, "\"t\":%ld,\"tf\":24", NOW + 3600);
    CHECK_CONTAINS(out, expect);
}

static void should_fail_when_no_utilization_headers() {
    char out[64];
    RateLimitHeaders h = { "", "", "", "", "", "", "", "" };
    PayloadOptions o = { NOW, 0, 0 };
    CHECK(!usage_payload_build(h, o, out, sizeof out));
    CHECK_EQ_STR(out, "{\"ok\":false}");
}

static void should_build_enterprise_payload_with_period_math() {
    // Reset at 2026-11-01 00:00:00 UTC → period 2026-10-01 .. 2026-11-01 (31 days).
    // NOW is 2026-10-10 12:00 → 9.5 / 31 = 30.6% → tp 31. sr = 21.5 days = 30960 min.
    char out[256];
    PayloadOptions o = { NOW, 0, 0 };
    CHECK(usage_payload_build(ent("0.555", "1793491200"), o, out, sizeof out));
    CHECK_EQ_STR(out, "{\"s\":56,\"sr\":30960,\"w\":0,\"wr\":0,\"st\":\"allowed\",\"acct\":\"ent\",\"tp\":31,\"pd\":31,\"rd\":\"Nov 1\",\"c\":1,\"ok\":true}");
}

static void should_clamp_previous_month_day_for_short_months() {
    // Reset 2026-03-31 00:00 UTC. Previous month is Feb (28 days in 2026) →
    // period start clamps to Feb 28 → 31-day period, "Mar 31".
    char out[256];
    long reset = 1774915200L;          // 2026-03-31T00:00:00Z
    long now   = reset - 86400;        // one day before reset
    char r[16]; snprintf(r, sizeof r, "%ld", reset);
    PayloadOptions o = { now, 0, 0 };
    CHECK(usage_payload_build(ent("0.1", r), o, out, sizeof out));
    CHECK_CONTAINS(out, "\"pd\":31,");
    CHECK_CONTAINS(out, "\"rd\":\"Mar 31\"");
    CHECK_CONTAINS(out, "\"tp\":97,");   // 30/31 = 96.8 → 97
}

static void should_use_local_time_for_enterprise_reset_date() {
    // Reset 2026-11-01 00:30 UTC, tz = -120 (UTC-2) → local date is Oct 31.
    char out[256];
    PayloadOptions o = { NOW, -120, 0 };
    CHECK(usage_payload_build(ent("0.2", "1793493000"), o, out, sizeof out));
    CHECK_CONTAINS(out, "\"rd\":\"Oct 31\"");
}

static void should_truncate_safely_into_small_buffer() {
    char out[16];
    PayloadOptions o = { NOW, 0, 0 };
    usage_payload_build(pro("0.5", "0", "0.5", "0"), o, out, sizeof out);
    CHECK(strlen(out) < sizeof out);
}

int main() {
    should_build_pro_payload_in_daemon_schema();
    should_clamp_reset_in_past_to_zero();
    should_emit_clock_fields_when_hour_format_set();
    should_fail_when_no_utilization_headers();
    should_build_enterprise_payload_with_period_math();
    should_clamp_previous_month_day_for_short_months();
    should_use_local_time_for_enterprise_reset_date();
    should_truncate_safely_into_small_buffer();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all usage_payload tests passed\n");
    return 0;
}
