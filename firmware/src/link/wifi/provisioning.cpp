#include "link/wifi/provisioning.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>

static const char* NS = "wifi";

void prov_load(WifiConfig* out) {
    memset(out, 0, sizeof *out);
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) return;   // namespace absent on first boot
    p.getString("ssid",  out->ssid,  sizeof out->ssid);
    p.getString("pass",  out->pass,  sizeof out->pass);
    p.getString("token", out->token, sizeof out->token);
    out->tz_minutes = p.getInt("tz", 0);
    out->hour_fmt   = p.getInt("tf", 0);
    p.end();
}

void prov_save(const WifiConfig& c) {
    Preferences p;
    p.begin(NS, false);
    p.putString("ssid",  c.ssid);
    p.putString("pass",  c.pass);
    p.putString("token", c.token);
    p.putInt("tz", c.tz_minutes);
    p.putInt("tf", c.hour_fmt);
    p.end();
}

void prov_clear(void) {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
}

bool prov_has_wifi(const WifiConfig& c)  { return c.ssid[0] != '\0'; }
bool prov_has_token(const WifiConfig& c) { return c.token[0] != '\0'; }

static void copy_arg(char* dst, size_t cap, const char* src) {
    while (*src == ' ') src++;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
    size_t n = strlen(dst);
    while (n && (dst[n-1] == ' ' || dst[n-1] == '\t')) dst[--n] = '\0';
}

// Never prints the token itself — only its length and last 4 chars.
static void print_status(const WifiConfig& c) {
    size_t tl = strlen(c.token);
    Serial.printf("wifi: ssid=\"%s\" pass=%s token=%s(%u chars%s%s) tz=%+d min hourfmt=%d\n",
        c.ssid, c.pass[0] ? "set" : "unset",
        tl ? "set" : "unset", (unsigned)tl,
        tl >= 4 ? ", ends " : "", tl >= 4 ? c.token + tl - 4 : "",
        c.tz_minutes, c.hour_fmt);
}

bool prov_handle_serial(const char* line, WifiConfig* cfg) {
    if (strncmp(line, "ssid ", 5) == 0)    { copy_arg(cfg->ssid,  sizeof cfg->ssid,  line + 5); prov_save(*cfg); Serial.println("ssid saved — reboot to apply"); return true; }
    if (strncmp(line, "pass ", 5) == 0)    { copy_arg(cfg->pass,  sizeof cfg->pass,  line + 5); prov_save(*cfg); Serial.println("pass saved — reboot to apply"); return true; }
    if (strncmp(line, "token ", 6) == 0)   { copy_arg(cfg->token, sizeof cfg->token, line + 6); prov_save(*cfg); Serial.println("token saved — reboot to apply"); return true; }
    if (strncmp(line, "tz ", 3) == 0)      { cfg->tz_minutes = atoi(line + 3); prov_save(*cfg); Serial.printf("tz=%d saved\n", cfg->tz_minutes); return true; }
    if (strncmp(line, "hourfmt ", 8) == 0) { int v = atoi(line + 8); cfg->hour_fmt = (v == 12 || v == 24) ? v : 0; prov_save(*cfg); Serial.printf("hourfmt=%d saved\n", cfg->hour_fmt); return true; }
    if (strcmp(line, "status") == 0)       { print_status(*cfg); return true; }
    if (strcmp(line, "wifi-reset") == 0)   { prov_clear(); memset(cfg, 0, sizeof *cfg); Serial.println("wifi credentials cleared — rebooting"); delay(200); ESP.restart(); return true; }
    return false;
}
