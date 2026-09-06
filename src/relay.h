// Remote control over WiFi via a relay you host (see relay/). The board keeps running
// BLE as normal; on a __RELAY__ command it also brings up WiFi STA and long-polls the
// relay for commands, running them through the SAME command handler and posting replies
// back. STA + BLE coexist (SoftAP is deliberately avoided). One command channel, two
// transports — the portal can drive the board locally over BLE or remotely via the relay.
#pragma once
#include <Arduino.h>

void        relayBegin();                                  // load saved url/token (no auto-connect)
bool        relayConnect(const String& url, const String& token);  // save + STA up + start polling
void        relayStop();                                   // stop polling (leaves WiFi as-is)
bool        relayActive();
int         relayState();                                  // 0 off · 1 connecting (WiFi down) · 2 online (polling)
const char* relayId();                                     // this board's mailbox id (config "relayid", else "bt-<mac6>")
void        relayRefreshId();                              // re-read the id after a config change
void        relayPostReply(const char* line);              // reply sink while running a relay command
void        relayTick();                                   // call from loop(): dispatch pulled commands
