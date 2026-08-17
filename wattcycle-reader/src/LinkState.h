// LinkState.h — connection state shared by every display implementation
// (BmsDisplay for the Heltec OLED, TftDisplay for the StamPLC TFT). Pulled
// out of BmsDisplay.h at M7a so a display class doesn't have to include
// another board's display header just to see this enum.
#ifndef LINK_STATE_H
#define LINK_STATE_H

#include <stdint.h>

enum class LinkState {
    Idle,
    Scanning,      // hollow indicator
    Connecting,
    Connected      // filled indicator
};

// Values older than this are shown as dashes rather than stale numbers, on
// every display implementation. A dropped link must never look like a live
// reading (§9).
static const uint32_t kStaleAfterMs = 15000;

#endif  // LINK_STATE_H
