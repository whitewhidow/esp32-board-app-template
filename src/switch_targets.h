// Firmware-switch targets: the OTHER apps' latest-release app-bin for THIS board.
// EMPTY by default — a standalone app has nothing to switch to, and the portal then
// shows no switch options (the 3-tap picker just reports "no targets").
//
// To join a SWITCH MESH (hop between sibling firmwares on the same board over OTA):
//   1. Fill SWITCH_TARGETS per board below with the siblings' release-bin URLs.
//   2. Give this app a DISTINCT `APP_BLE_MAC_TAG` in board.h so the host's GATT
//      cache doesn't collide with the siblings.
//   3. Each target must be the SAME chip with an IDENTICAL A/B partition table
//      (app slots at the same offsets), and publish an app-only bin for the
//      matching board env. Data note: NVS config carries across a switch; anything
//      in the filesystem (LittleFS) does not. See GOTCHAS.md.
//
// Example — uncomment and edit, then delete the empty defaults below:
//   #if defined(APP_BOARD_TEMBED)
//   static const SwitchTarget SWITCH_TARGETS[] = {
//     { "OtherApp", "https://github.com/you/otherapp/releases/latest/download/otherapp-app-tembed-cc1101.bin" },
//   };
//   static const int SWITCH_TARGET_COUNT = (int)(sizeof(SWITCH_TARGETS)/sizeof(SWITCH_TARGETS[0]));
//   #else
//   ... (dummy + 0 for boards not in the mesh) ...
//   #endif
#pragma once

struct SwitchTarget { const char* name; const char* url; };

// No siblings by default. Replace with per-board lists (see the example above).
static const SwitchTarget SWITCH_TARGETS[1] = { { "", "" } };   // dummy; unused while count is 0
static const int SWITCH_TARGET_COUNT = 0;
