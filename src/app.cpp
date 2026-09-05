// >>> YOUR APP LIVES HERE <<<
// Demo: edit a text file that lives in LittleFS, over BLE, from the portal page.
// It shows the two things every board app eventually needs — persistent storage
// and moving a payload bigger than one BLE packet — using the built-in RX/TX
// control characteristic (chunked + base64, one ack per chunk). Replace it with
// your own logic; keep whichever pieces you need.
#include "app.h"
#include "ble_control.h"
#include "display.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "mbedtls/base64.h"

#define NOTE_FILE "/note.txt"
#define NOTE_TMP  "/note.tmp"
#define NOTE_DEFAULT "Hello from LittleFS!\nEdit this over BLE and press Save."
#define RAW_CHUNK 120     // raw bytes per chunk; base64 -> ~160 chars, fits the 320B RX buffer + BLE MTU

// --- base64 (each chunk is encoded standalone so it decodes independently) ---
static String b64enc(const uint8_t* d, size_t n) {
  unsigned char out[4 * ((RAW_CHUNK + 2) / 3) + 4]; size_t olen = 0;
  if (mbedtls_base64_encode(out, sizeof(out), &olen, d, n) != 0) return "";
  return String((char*)out).substring(0, olen);
}
static size_t b64dec(const char* b, uint8_t* o, size_t max) {
  size_t olen = 0;
  if (mbedtls_base64_decode(o, max, &olen, (const unsigned char*)b, strlen(b)) != 0) return 0;
  return olen;
}

static File   s_put;         // temp file open during an upload
static size_t s_upTotal = 0;

void appSetup() {
  // The FS partition is named "spiffs" (see partitions-*.csv), so the default
  // LittleFS.begin() finds it. formatOnFail=true makes first boot self-heal.
  bool fs = LittleFS.begin(true);
  Serial.printf("[app] LittleFS %s\n", fs ? "mounted" : "MOUNT FAILED");
  if (!LittleFS.exists(NOTE_FILE)) {
    File f = LittleFS.open(NOTE_FILE, FILE_WRITE);
    if (f) { f.print(NOTE_DEFAULT); f.close(); }
  }
}

void appLoop() {
  // nothing periodic in the demo
}

// Portal commands: __NOTEGET__:<off> · __NOTEPUT__ · __NOTEADD__:<b64> · __NOTEEND__
bool appHandleCommand(const char* cmd) {
  if (!strncmp(cmd, "__NOTEGET__:", 12)) {                 // download a chunk at <off>
    size_t off = strtoul(cmd + 12, nullptr, 10);
    uint8_t raw[RAW_CHUNK]; int got = 0; size_t total = 0;
    File f = LittleFS.open(NOTE_FILE, FILE_READ);
    if (f) { total = f.size(); if (off) f.seek(off); got = f.read(raw, RAW_CHUNK); if (got < 0) got = 0; f.close(); }
    String line = "note:" + String(off + got) + ":" + String(total) + ":";
    if (got > 0) line += b64enc(raw, got);
    bleNotify(line.c_str());
    return true;
  }
  if (!strcmp(cmd, "__NOTEPUT__")) {                       // begin an upload (truncate temp)
    s_upTotal = 0; s_put = LittleFS.open(NOTE_TMP, FILE_WRITE);
    bleNotify(s_put ? "note:ready" : "note:err");
    return true;
  }
  if (!strncmp(cmd, "__NOTEADD__:", 12)) {                 // append one decoded chunk
    uint8_t raw[RAW_CHUNK + 8];
    size_t n = b64dec(cmd + 12, raw, sizeof(raw));
    bool ok = s_put && n && (s_put.write(raw, n) == n);
    if (ok) s_upTotal += n;
    bleNotify(ok ? (String("note:ack:") + s_upTotal).c_str() : "note:ack:err");
    return true;
  }
  if (!strcmp(cmd, "__NOTEDEFAULT__")) {                   // restore the note to its built-in default
    File f = LittleFS.open(NOTE_FILE, FILE_WRITE);
    if (f) { f.print(NOTE_DEFAULT); f.close(); }
    bleNotify("note:default");
    return true;
  }
  if (!strcmp(cmd, "__NOTEEND__")) {                       // commit temp -> live (size read AFTER close)
    size_t sz = 0;
    if (s_put) {
      s_put.flush(); s_put.close();
      File t = LittleFS.open(NOTE_TMP, FILE_READ); if (t) { sz = t.size(); t.close(); }
      if (sz > 0) { LittleFS.remove(NOTE_FILE); LittleFS.rename(NOTE_TMP, NOTE_FILE); }
      else LittleFS.remove(NOTE_TMP);              // never clobber with an empty file
    }
    dispCenter("NOTE", (String(sz) + " bytes saved").c_str(), 0x3FB950);
    bleNotify((String("note:done:") + sz).c_str());
    return true;
  }
  return false;                                            // not ours -> "err:unknown"
}
