// See relay.h.
#include "relay.h"
#include "ble_control.h"
#include "netota.h"
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
static QueueHandle_t s_cmdQ   = nullptr;       // pull task -> main loop
struct RelayMsg { char s[320]; };

static bool isHttps() { return s_url.startsWith("https"); }

static void computeId() {
  String m = bleMac() ? String(bleMac()) : String("000000");
  m.replace(":", "");
  if (m.length() > 6) m = m.substring(m.length() - 6);
  s_id = "bt-" + m;
}
const char* relayId() { if (!s_id.length()) computeId(); return s_id.c_str(); }
bool relayActive() { return s_active; }
int  relayState()  { return s_state; }

// Blocking long-poll for one command; "" on timeout/error. The client is stack-local so
// it stays alive across the GET; https and plain http need different client types.
static String pullOne() {
  if (WiFi.status() != WL_CONNECTED) return String();
  String u = s_url + "/pull/" + s_id, out;
  HTTPClient http; http.setTimeout(28000);
  if (isHttps()) {
    WiFiClientSecure c; c.setInsecure();
    if (!http.begin(c, u)) return out;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok);
    if (http.GET() == 200) out = http.getString();
    http.end();
  } else {
    WiFiClient c;
    if (!http.begin(c, u)) return out;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok);
    if (http.GET() == 200) out = http.getString();
    http.end();
  }
  return out;
}

void relayPostReply(const char* line) {
  if (!s_active || WiFi.status() != WL_CONNECTED) return;
  String u = s_url + "/reply/" + s_id;
  HTTPClient http; http.setTimeout(8000);
  if (isHttps()) {
    WiFiClientSecure c; c.setInsecure();
    if (!http.begin(c, u)) return;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok);
    http.addHeader("Content-Type", "text/plain");
    http.POST((uint8_t*)line, strlen(line)); http.end();
  } else {
    WiFiClient c;
    if (!http.begin(c, u)) return;
    if (s_tok.length()) http.addHeader("x-relay-token", s_tok);
    http.addHeader("Content-Type", "text/plain");
    http.POST((uint8_t*)line, strlen(line)); http.end();
  }
}

// The HTTP long-poll lives in its own task so a 25s poll never stalls loop()/BLE.
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
    String cmd = pullOne();
    if (cmd.length()) { Serial.printf("[relay] cmd: %s\n", cmd.c_str()); RelayMsg m; strlcpy(m.s, cmd.c_str(), sizeof(m.s)); xQueueSend(s_cmdQ, &m, 0); }
    else vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void relayBegin() { /* relay URL/token now live in the app config (relayurl/relaytok); no auto-connect */ }

bool relayConnect(const String& url, const String& token) {
  s_url = url; s_url.trim(); while (s_url.endsWith("/")) s_url.remove(s_url.length() - 1);
  s_tok = token;
  computeId();
  s_state = 1;
  Serial.printf("[relay] go remote: %s as %s — bringing up WiFi STA\n", s_url.c_str(), s_id.c_str());
  netConnect();                                  // STA up with the saved WiFi creds
  s_active = true;
  if (!s_cmdQ) s_cmdQ = xQueueCreate(8, sizeof(RelayMsg));
  if (!s_task) xTaskCreatePinnedToCore(relayTask, "relay", 8192, nullptr, 1, &s_task, 0);
  return true;
}

void relayStop() { s_active = false; s_state = 0; Serial.println("[relay] stopped"); }

// Runs in loop(): dispatch any pulled commands through the shared handler. bleNotify is
// routed to relayPostReply for the duration of each (see bleHandleExternal).
void relayTick() {
  if (!s_cmdQ) return;
  RelayMsg m;
  while (xQueueReceive(s_cmdQ, &m, 0)) bleHandleExternal(m.s);
}
