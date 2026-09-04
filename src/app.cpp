// >>> YOUR APP LIVES HERE <<<
// This demo just echoes text from the portal onto the board (and acks over BLE),
// to show the wiring end to end. Replace it with your own logic.
#include "app.h"
#include "ble_control.h"
#include "display.h"
#include <Arduino.h>

void appSetup() {
  // TODO: initialise your app (sensors, radios, extra BLE services, …).
}

void appLoop() {
  // TODO: your periodic work. Called every loop() iteration.
}

// The portal writes "__CMD__" strings to the control service. Built-in commands
// (__VER__ / __WIFI__ / __OTA__ / __STATUS__ / …) are handled before this; here you
// handle YOUR commands. Return true if you handled it.
bool appHandleCommand(const char* cmd) {
  if (!strncmp(cmd, "__ECHO__:", 9)) {          // demo: portal -> board screen + ack
    dispCenter("ECHO", cmd + 9, 0x22D3E0);
    bleNotify("echo:ok");
    return true;
  }
  return false;                                  // not ours -> "err:unknown"
}
