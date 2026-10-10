#include "link/wifi/usage_payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---- small civil-calendar helpers (tz applied by caller) -------------------
static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int  days_in_month(int y, int m) {           // m = 1..12
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}
// Howard Hinnant's days_from_civil / civil_from_days (proleptic Gregorian).
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
static void civil_from_days(long z, int* y, int* m, int* d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long yy = (long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

static int  round_pct(double util) { return (int)lround(util * 100.0); }
static long mins_until(long reset, long now) {
    if (reset <= 0) return 0;
    double m = (double)(reset - now) / 60.0;
    return m > 0 ? lround(m) : 0;
}

bool usage_payload_build(const RateLimitHeaders& h, const PayloadOptions& o,
                         char* out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';

    char clock[48] = "";
    if (o.hour_fmt == 12 || o.hour_fmt == 24)
        snprintf(clock, sizeof clock, ",\"t\":%ld,\"tf\":%d",
                 o.now_utc + (long)o.tz_minutes * 60, o.hour_fmt);

    if (h.util_5h && *h.util_5h) {
        // Pro / Max: 5h + 7d windows
        int  sp = round_pct(atof(h.util_5h));
        long sr = mins_until(atol(h.reset_5h ? h.reset_5h : "0"), o.now_utc);
        int  wp = (h.util_7d && *h.util_7d) ? round_pct(atof(h.util_7d)) : 0;
        long wr = mins_until(atol(h.reset_7d ? h.reset_7d : "0"), o.now_utc);
        const char* st = (h.status_5h && *h.status_5h) ? h.status_5h : "unknown";
        snprintf(out, cap,
            "{\"s\":%d,\"sr\":%ld,\"w\":%d,\"wr\":%ld,\"st\":\"%s\",\"acct\":\"pro\"%s,\"c\":1,\"ok\":true}",
            sp, sr, wp, wr, st, clock);
        return true;
    }

    if (h.overage_util && *h.overage_util) {
        // Enterprise: spending limit over a calendar-monthly period that ends
        // at overage_reset. Period start = same day/time one month earlier
        // (clamped to the shorter month), computed in *local* time like the
        // daemon's datetime.fromtimestamp.
        int  sp = round_pct(atof(h.overage_util));
        long reset = atol(h.overage_reset ? h.overage_reset : "0");
        long sr = mins_until(reset, o.now_utc);
        const char* st = (h.status && *h.status) ? h.status : "unknown";

        int tp = 0, pd = 30;
        char rd[12] = "";
        if (reset > 0) {
            long off = (long)o.tz_minutes * 60;
            long lreset = reset + off;
            long days = lreset / 86400; if (lreset % 86400 < 0) days--;   // floor
            long secs = lreset - days * 86400;
            int y, m, d; civil_from_days(days, &y, &m, &d);
            int pm = (m == 1) ? 12 : m - 1;
            int py = (m == 1) ? y - 1 : y;
            int dim = days_in_month(py, pm);
            int pdd = d < dim ? d : dim;
            long lstart = days_from_civil(py, pm, pdd) * 86400 + secs;
            long len = lreset - lstart;
            if (len > 0) {
                double pct = (double)((o.now_utc + off) - lstart) / (double)len * 100.0;
                int t = (int)lround(pct);
                tp = t < 0 ? 0 : t > 100 ? 100 : t;
                pd = (int)lround((double)len / 86400.0);
            }
            static const char* MON[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
            snprintf(rd, sizeof rd, "%s %d", MON[m - 1], d);
        }
        snprintf(out, cap,
            "{\"s\":%d,\"sr\":%ld,\"w\":0,\"wr\":0,\"st\":\"%s\",\"acct\":\"ent\",\"tp\":%d,\"pd\":%d,\"rd\":\"%s\"%s,\"c\":1,\"ok\":true}",
            sp, sr, st, tp, pd, rd, clock);
        return true;
    }

    snprintf(out, cap, "{\"ok\":false}");
    return false;
}
