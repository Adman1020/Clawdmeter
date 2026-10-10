#include "link/wifi/captive_portal.h"
#include "link/wifi/provisioning.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

static WebServer* s_http = nullptr;
static DNSServer* s_dns  = nullptr;
static bool       s_active = false;
static WifiConfig s_cfg;
static IPAddress  s_ip(192, 168, 4, 1);

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clawdmeter setup</title>
<style>body{font-family:system-ui,sans-serif;background:#141413;color:#faf9f5;margin:0;padding:24px}
h1{font-size:22px;margin:0 0 4px}p{color:#b0aea5;margin:0 0 20px}label{display:block;margin:14px 0 6px}
input,select{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;border:1px solid #444;background:#1f1e1d;color:#faf9f5;font-size:16px}
button{margin-top:22px;width:100%;padding:14px;border:0;border-radius:8px;background:#d97757;color:#141413;font-size:17px;font-weight:600}
small{color:#8a877f}code{color:#faf9f5}</style></head><body><h1>Clawdmeter</h1><p>Standalone WiFi setup</p><form method="POST" action="/save">)HTML";

static const char PAGE_TAIL[] PROGMEM = R"HTML(<button type="submit">Save &amp; connect</button>
<p style="margin-top:18px"><small>Token: run <code>claude setup-token</code> on a machine with Claude Code and paste the result (starts with sk-ant-oat01-). It is stored on this device (plain flash) and sent only to api.anthropic.com.</small></p>
</form></body></html>)HTML";

struct TzOpt { int mins; const char* label; };
static const TzOpt TZ_OPTS[] = {
    {-720,"UTC-12"},{-600,"UTC-10 Hawaii"},{-540,"UTC-9 Alaska"},{-480,"UTC-8 Pacific"},
    {-420,"UTC-7 Mountain"},{-360,"UTC-6 Central"},{-300,"UTC-5 Eastern"},{-240,"UTC-4 Atlantic"},
    {-180,"UTC-3"},{-120,"UTC-2"},{-60,"UTC-1"},{0,"UTC+0 London/Dublin"},{60,"UTC+1 Central Europe"},
    {120,"UTC+2"},{180,"UTC+3"},{240,"UTC+4"},{300,"UTC+5"},{330,"UTC+5:30 India"},{345,"UTC+5:45 Nepal"},
    {360,"UTC+6"},{420,"UTC+7"},{480,"UTC+8 China/Singapore"},{540,"UTC+9 Japan"},{570,"UTC+9:30 Adelaide"},
    {600,"UTC+10 Sydney"},{660,"UTC+11"},{720,"UTC+12 NZ"},{780,"UTC+13"},
};

// <select>s pre-select the stored values so re-provisioning for a new network
// does not silently reset the clock settings.
static void append_selects(String& page, int tz, int tf) {
    page += "<label>Timezone offset</label><select name=\"tz\">";
    bool matched = false;
    for (const TzOpt& o : TZ_OPTS) if (o.mins == tz) matched = true;
    for (const TzOpt& o : TZ_OPTS) {
        page += "<option value=\""; page += o.mins; page += "\"";
        if (o.mins == tz || (!matched && o.mins == 0)) page += " selected";
        page += ">"; page += o.label; page += "</option>";
    }
    page += "</select><label>Clock on usage screen</label><select name=\"tf\">";
    page += tf == 0  ? "<option value=\"0\" selected>Off</option>"      : "<option value=\"0\">Off</option>";
    page += tf == 24 ? "<option value=\"24\" selected>24-hour</option>" : "<option value=\"24\">24-hour</option>";
    page += tf == 12 ? "<option value=\"12\" selected>12-hour</option>" : "<option value=\"12\">12-hour</option>";
    page += "</select>";
}

static void html_escape(const char* in, String& out) {
    for (; *in; ++in) {
        switch (*in) {
            case '&': out += "&amp;"; break; case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;  case '"': out += "&quot;"; break;
            default: out += *in;
        }
    }
}

static void handle_root() {
    String page; page.reserve(4096);
    page += FPSTR(PAGE_HEAD);
    page += "<label>WiFi network</label><input name=\"ssid\" required maxlength=\"32\" value=\"";
    html_escape(s_cfg.ssid, page);
    // Never echo the stored password: this is an open AP. Empty = keep current.
    page += "\"><label>WiFi password</label><input name=\"pass\" type=\"password\" maxlength=\"64\" placeholder=\"";
    page += s_cfg.pass[0] ? "(keep current password)" : "";
    page += "\"><label>Anthropic token</label><input name=\"token\" maxlength=\"255\" placeholder=\"";
    page += s_cfg.token[0] ? "(keep current token)" : "sk-ant-oat01-…";
    page += "\">";
    append_selects(page, s_cfg.tz_minutes, s_cfg.hour_fmt);
    page += FPSTR(PAGE_TAIL);
    s_http->send(200, "text/html", page);
}

static void copy_field(char* dst, size_t cap, const String& v) {
    strncpy(dst, v.c_str(), cap - 1);
    dst[cap - 1] = '\0';
}

static void handle_save() {
    // WebServer::arg() URL-decodes form bodies, so &, +, % and quotes in a
    // password arrive verbatim.
    String ssid = s_http->arg("ssid"); ssid.trim();
    if (!ssid.length()) { s_http->send(400, "text/plain", "WiFi network name is required"); return; }
    WifiConfig c = s_cfg;
    String pass = s_http->arg("pass");
    String tok  = s_http->arg("token"); tok.trim();
    bool creds_changed = (ssid != s_cfg.ssid) || (pass.length() > 0);
    copy_field(c.ssid, sizeof c.ssid, ssid);
    if (pass.length()) copy_field(c.pass, sizeof c.pass, pass);     // empty = keep existing
    if (tok.length())  copy_field(c.token, sizeof c.token, tok);    // empty = keep existing
    c.tz_minutes = s_http->arg("tz").toInt();
    int tf = s_http->arg("tf").toInt(); c.hour_fmt = (tf == 12 || tf == 24) ? tf : 0;
    if (creds_changed) c.ever_ok = false;   // new credentials must prove themselves again
    prov_save(c);
    s_http->send(200, "text/html",
        "<!doctype html><body style=\"font-family:system-ui;background:#141413;color:#faf9f5;padding:24px\">"
        "<h1>Saved</h1><p>Clawdmeter is rebooting and joining your WiFi. You can close this page.</p></body>");
    delay(800);
    ESP.restart();
}

static void handle_redirect() {
    s_http->sendHeader("Location", "http://192.168.4.1/", true);
    s_http->send(302, "text/plain", "");
}

void portal_start(const WifiConfig& current) {
    if (s_active) return;
    s_cfg = current;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(s_ip, s_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Clawdmeter");
    s_dns = new DNSServer();
    s_dns->setErrorReplyCode(DNSReplyCode::NoError);
    s_dns->start(53, "*", s_ip);
    s_http = new WebServer(80);
    s_http->on("/", HTTP_GET, handle_root);
    s_http->on("/save", HTTP_POST, handle_save);
    // OS captive-portal probes → redirect so the "sign in" sheet appears
    s_http->on("/generate_204", handle_redirect);        // Android
    s_http->on("/gen_204", handle_redirect);
    s_http->on("/hotspot-detect.html", handle_redirect); // iOS / macOS
    s_http->on("/library/test/success.html", handle_redirect);
    s_http->on("/connecttest.txt", handle_redirect);     // Windows
    s_http->on("/ncsi.txt", handle_redirect);
    s_http->on("/redirect", handle_redirect);
    s_http->onNotFound(handle_redirect);
    s_http->begin();
    s_active = true;
}

void portal_tick(void) {
    if (!s_active) return;
    s_dns->processNextRequest();
    s_http->handleClient();
}

void portal_stop(void) {
    if (!s_active) return;
    s_http->stop(); delete s_http; s_http = nullptr;
    s_dns->stop();  delete s_dns;  s_dns  = nullptr;
    WiFi.softAPdisconnect(true);
    s_active = false;
}

bool portal_active(void) { return s_active; }
