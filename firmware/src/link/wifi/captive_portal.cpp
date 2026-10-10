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

static const char PAGE_TAIL[] PROGMEM = R"HTML(<label>Timezone offset</label><select name="tz">
<option value="-720">UTC-12</option><option value="-600">UTC-10 Hawaii</option><option value="-540">UTC-9 Alaska</option>
<option value="-480">UTC-8 Pacific</option><option value="-420">UTC-7 Mountain</option><option value="-360">UTC-6 Central</option>
<option value="-300">UTC-5 Eastern</option><option value="-240">UTC-4 Atlantic</option><option value="-180">UTC-3</option>
<option value="0" selected>UTC+0 London/Dublin</option><option value="60">UTC+1 Central Europe</option><option value="120">UTC+2</option>
<option value="180">UTC+3</option><option value="240">UTC+4</option><option value="330">UTC+5:30 India</option><option value="480">UTC+8 China/Singapore</option>
<option value="540">UTC+9 Japan</option><option value="600">UTC+10 Sydney</option><option value="720">UTC+12 NZ</option></select>
<label>Clock on usage screen</label><select name="tf"><option value="0">Off</option><option value="24" selected>24-hour</option><option value="12">12-hour</option></select>
<button type="submit">Save &amp; connect</button>
<p style="margin-top:18px"><small>Token: run <code>claude setup-token</code> on a machine with Claude Code and paste the result (starts with sk-ant-oat01-). It is stored on this device only.</small></p>
</form></body></html>)HTML";

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
    String page; page.reserve(3200);
    page += FPSTR(PAGE_HEAD);
    page += "<label>WiFi network</label><input name=\"ssid\" required maxlength=\"32\" value=\"";
    html_escape(s_cfg.ssid, page);
    page += "\"><label>WiFi password</label><input name=\"pass\" type=\"password\" maxlength=\"64\" value=\"";
    html_escape(s_cfg.pass, page);
    page += "\"><label>Anthropic token</label><input name=\"token\" maxlength=\"255\" placeholder=\"";
    page += s_cfg.token[0] ? "(keep current token)" : "sk-ant-oat01-…";
    page += "\">";
    page += FPSTR(PAGE_TAIL);
    s_http->send(200, "text/html", page);
}

static void handle_save() {
    WifiConfig c = s_cfg;
    // WebServer::arg() URL-decodes form bodies, so &, +, % and quotes in a
    // password arrive verbatim.
    strncpy(c.ssid, s_http->arg("ssid").c_str(), sizeof c.ssid - 1);
    strncpy(c.pass, s_http->arg("pass").c_str(), sizeof c.pass - 1);
    String tok = s_http->arg("token"); tok.trim();
    if (tok.length()) strncpy(c.token, tok.c_str(), sizeof c.token - 1);   // empty = keep existing
    c.tz_minutes = s_http->arg("tz").toInt();
    int tf = s_http->arg("tf").toInt(); c.hour_fmt = (tf == 12 || tf == 24) ? tf : 0;
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
    Serial.println("portal: hotspot \"Clawdmeter\" up at 192.168.4.1");
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
