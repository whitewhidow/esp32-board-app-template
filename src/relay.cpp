// See relay.h.
#include "relay.h"
#include "ble_control.h"
#include "netota.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static String        s_url, s_tok, s_id;
static volatile bool s_active = false;
static volatile int  s_state  = 0;             // 0 off, 1 connecting (WiFi down), 2 online (polling)
static TaskHandle_t  s_task   = nullptr;
static QueueHandle_t s_cmdQ   = nullptr;       // task -> main loop  (pulled commands)
static QueueHandle_t s_replyQ = nullptr;       // main loop -> task  (replies to POST)
struct RelayMsg { char s[320]; };

static bool isHttps() { return s_url.startsWith("https"); }
// Normalise: strip trailing '/', lowercase the scheme (mobile keyboards auto-capitalise the
// first letter -> "Https://", which would otherwise be treated as plain http).
static void normUrl(String& u) {
  u.trim(); while (u.endsWith("/")) u.remove(u.length() - 1);
  int p = u.indexOf("://"); if (p > 0) { String sc = u.substring(0, p); sc.toLowerCase(); u = sc + u.substring(p); }
}

static void computeId() {
  String cid = cfgGet("relayid", ""); cid.trim(); cid.replace(" ", ""); cid.replace("/", "");
  if (cid.length()) { s_id = cid; return; }               // custom id from config
  String m = bleMac() ? String(bleMac()) : String("000000");   // default: bt-<last 6 hex of BLE MAC>
  m.replace(":", "");
  if (m.length() > 6) m = m.substring(m.length() - 6);
  s_id = "bt-" + m;
}
const char* relayId() { if (!s_id.length()) computeId(); return s_id.c_str(); }
void relayRefreshId() { s_id = ""; computeId(); }
bool relayActive() { return s_active; }
int  relayState()  { return s_state; }

// ---- HTTP helpers — ONLY ever called from the relay task, so at most one TLS context
// exists at a time (critical on the no-PSRAM C5: two concurrent TLS won't fit in heap). ----
static String httpPull() {                       // GET /pull — modest timeout so idle cycles stay short
  String u = s_url + "/pull/" + s_id, out;
  HTTPClient http; http.setTimeout(6000);
  if (isHttps()) { WiFiClientSecure c; c.setInsecure(); if (!http.begin(c, u)) return out;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); if (http.GET() == 200) out = http.getString(); http.end(); }
  else { WiFiClient c; if (!http.begin(c, u)) return out;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); if (http.GET() == 200) out = http.getString(); http.end(); }
  return out;
}
static bool httpPostReplyOnce(const String& u, const char* line) {
  HTTPClient http; http.setTimeout(8000); int code = 0;
  if (isHttps()) { WiFiClientSecure c; c.setInsecure(); if (!http.begin(c, u)) return false;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); http.addHeader("Content-Type", "text/plain"); code = http.POST((uint8_t*)line, strlen(line)); http.end(); }
  else { WiFiClient c; if (!http.begin(c, u)) return false;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok); http.addHeader("Content-Type", "text/plain"); code = http.POST((uint8_t*)line, strlen(line)); http.end(); }
  return code == 200;
}
static void httpPostReply(const char* line) {    // POST /reply — retry: C5 TLS alloc is marginal and often
  String u = s_url + "/reply/" + s_id;           // succeeds on the 2nd try once a little heap frees up
  for (int i = 0; i < 4; i++) { if (httpPostReplyOnce(u, line)) return; vTaskDelay(pdMS_TO_TICKS(150)); }
  Serial.println("[relay] reply POST failed after retries");
}

// Reply sink (runs in the main loop while a relay command is handled): just queue it —
// the task does the actual POST, so HTTP never overlaps the pull.
void relayPostReply(const char* line) {
  if (!s_active || !s_replyQ) return;
  RelayMsg m; strlcpy(m.s, line, sizeof(m.s));
  xQueueSend(s_replyQ, &m, pdMS_TO_TICKS(4000));   // block if full — don't drop burst chunks
}

// One task owns ALL HTTP: drain outgoing replies, then pull one command. Because a command
// POST from the portal wakes a parked pull instantly, an active exchange still round-trips
// fast; the 6s pull timeout only bounds idle cycles.
static void relayTask(void*) {
  bool wasUp = false; uint32_t lastLog = 0;
  for (;;) {
    if (!s_active) { s_state = 0; wasUp = false; vTaskDelay(pdMS_TO_TICKS(400)); continue; }
    if (WiFi.status() != WL_CONNECTED) {
      s_state = 1; wasUp = false;
      if (millis() - lastLog > 3000) { lastLog = millis(); Serial.printf("[relay] waiting for WiFi (status=%d)\n", WiFi.status()); }
      vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    if (!wasUp) { wasUp = true; s_state = 2; Serial.printf("[relay] WiFi up, IP %s — polling %s/pull/%s\n", WiFi.localIP().toString().c_str(), s_url.c_str(), s_id.c_str()); }
    RelayMsg m;
    while (xQueueReceive(s_replyQ, &m, 0)) httpPostReply(m.s);     // send pending replies first (single TLS)
    String cmd = httpPull();
    if (cmd.length()) { Serial.printf("[relay] cmd: %s\n", cmd.c_str()); RelayMsg c; strlcpy(c.s, cmd.c_str(), sizeof(c.s)); xQueueSend(s_cmdQ, &c, pdMS_TO_TICKS(4000));   // block if main loop behind — don't drop commands
      vTaskDelay(pdMS_TO_TICKS(40)); }                            // let the main loop produce the reply before the next pull
    else vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void relayBegin() {
  // Optional auto-go-remote at boot (config "relayauto") — for a headless/deployed board
  // that should come back online after a power blip. Needs a Relay URL + saved WiFi creds.
  if (cfgGet("relayauto", "0") != "1") return;
  String url = cfgGet("relayurl", "");
  if (!url.length() || !netConfigured()) { Serial.println("[relay] auto-boot skipped (no URL or WiFi creds)"); return; }
  Serial.println("[relay] auto-connect on boot");
  relayConnect(url, cfgGet("relaytok", ""));
  bool keepBle = (cfgGet("relaykeepble", "0") == "1") || (ESP.getPsramSize() > 0);
  if (!keepBle) { delay(200); bleStop(); }   // no PSRAM -> free heap for TLS
}

bool relayConnect(const String& url, const String& token) {
  s_url = url; normUrl(s_url);
  s_tok = token;
  computeId();
  s_state = 1;
  Serial.printf("[relay] go remote: %s as %s — bringing up WiFi STA\n", s_url.c_str(), s_id.c_str());
  netConnect();                                  // STA up with the saved WiFi creds
  s_active = true;
  if (!s_cmdQ)   s_cmdQ   = xQueueCreate(16, sizeof(RelayMsg));
  if (!s_replyQ) s_replyQ = xQueueCreate(16, sizeof(RelayMsg));
  if (!s_task)   xTaskCreatePinnedToCore(relayTask, "relay", 8192, nullptr, 1, &s_task, 0);
  return true;
}

void relayStop() { s_active = false; s_state = 0; Serial.println("[relay] stopped"); }

// Runs in loop(): dispatch any pulled commands through the shared handler. bleNotify is
// routed to relayPostReply (queue only) for the duration of each (see bleHandleExternal).
void relayTick() {
  if (!s_cmdQ) return;
  RelayMsg m;
  while (xQueueReceive(s_cmdQ, &m, 0)) bleHandleExternal(m.s);
}
