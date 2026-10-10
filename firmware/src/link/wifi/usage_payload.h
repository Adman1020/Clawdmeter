#pragma once
#include <stddef.h>

// Pure (no Arduino, no heap) builder that turns the Anthropic rate-limit
// response headers into the JSON payload the host daemon sends today, so the
// firmware's parse_json / usage_rate / UI paths need no WiFi-specific code.
// Mirrors daemon/claude-usage-daemon.sh build_payload_for_token().

struct RateLimitHeaders {               // raw header values, "" when absent
    const char* util_5h;   const char* reset_5h;   const char* status_5h;
    const char* util_7d;   const char* reset_7d;
    const char* status;                             // anthropic-ratelimit-unified-status
    const char* overage_util; const char* overage_reset;
};

struct PayloadOptions {
    long now_utc;      // seconds since epoch, UTC
    int  tz_minutes;   // local offset east of UTC (e.g. 60 for UTC+1)
    int  hour_fmt;     // 12 or 24; 0 = omit clock fields
};

// Returns false (and writes "{\"ok\":false}") when neither a 5h nor an
// overage utilization header is present. `out` is always NUL-terminated.
bool usage_payload_build(const RateLimitHeaders& h, const PayloadOptions& o,
                         char* out, size_t cap);
