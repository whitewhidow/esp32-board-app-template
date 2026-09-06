// See config.h. Backed by NVS (Preferences), namespace "cfg".
#include "config.h"
#include <Preferences.h>

// >>> YOUR APP SETTINGS <<< — add fields here; the portal auto-renders a form and
// stores each by key. type: 's' text, 'n' number, 'b' 0/1 (checkbox).
const CfgField CFG_FIELDS[] = {
  { "name",       "Device name",         's' },
  { "brightness", "Brightness (0-255)",  'n' },
  { "splash",     "Boot splash",         'b' },   // default on (unset -> on; see main.cpp)
  { "relayurl",   "Relay URL (remote control)", 's' },   // set this to enable "Go remote" — see relay/
  { "relaytok",   "Relay token",         's' },
  { "relayid",    "Relay id (blank = auto)", 's' },       // mailbox id; blank -> bt-<BLE MAC6>
  { "relayauto",  "Connect relay on boot",   'b' },       // auto-go-remote at startup (needs URL + WiFi)
  // { "beep",    "Beep on event",       'b' },
};
const int CFG_FIELD_COUNT = sizeof(CFG_FIELDS) / sizeof(CFG_FIELDS[0]);

static Preferences s_p;

String cfgGet(const char* key, const char* def) {
  s_p.begin("cfg", true); String v = s_p.getString(key, def); s_p.end(); return v;
}
void cfgSet(const char* key, const char* val) {
  s_p.begin("cfg", false); s_p.putString(key, val); s_p.end();
}

static String jesc(const String& in) {
  String o; for (char c : in) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o;
}
String cfgJson() {
  String s = "cfg:[";
  for (int i = 0; i < CFG_FIELD_COUNT; i++) {
    if (i) s += ',';
    s += "{\"k\":\""; s += CFG_FIELDS[i].key;
    s += "\",\"l\":\""; s += CFG_FIELDS[i].label;
    s += "\",\"t\":\""; s += CFG_FIELDS[i].type;
    s += "\",\"v\":\""; s += jesc(cfgGet(CFG_FIELDS[i].key)); s += "\"}";
  }
  s += "]"; return s;
}
