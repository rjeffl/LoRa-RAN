// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Lights a Fluvy scenes-card tile when it is pressed (HA Integration Implementation Plan 8.3).
// Fluvy's only press feedback is a 2.5 % shrink while the finger is down, which is hard to see
// in some display modes, and its lit state follows a timestamp that a cover or switch does
// not have. Fluvy offers no setting for either, so this module adds the flash from outside.
//
// It says the tap registered, and nothing about whether the gate moved: Gate Status does that.
// It acts on the Gate dashboard only. Restore defaults on LRAN Configuration is also a scenes
// tile, and it opens a confirmation first, so a flash there would look like a send.

const kFlashMs = 1500;
const kDashboardPath = "/lran-gate";

// Fluvy's own class names, read from its 1.11.3 bundle. If an update renames them the flash
// stops and nothing else changes, so the render check after a Fluvy update has to press a tile.
const kTileSelector = "button.am-scene:not(.fv-tile--off)";

const kLit = {
  backgroundColor: "var(--fluvy-accent)",
  boxShadow: "inset 0 0 0 3px var(--fluvy-accent-hover)",
};

// click rather than pointerdown, so a scroll that starts on a tile does not flash it. Capture
// on window, because the tile is inside the card's shadow root; composedPath() reaches it.
window.addEventListener(
  "click",
  (event) => {
    if (!location.pathname.startsWith(kDashboardPath)) return;
    const tile = event
      .composedPath()
      .find((node) => node instanceof Element && node.matches(kTileSelector));
    if (!tile) return;
    // Held for 60 % of the run, then fades back to the tile's own colours.
    tile.animate(
      [
        { ...kLit, offset: 0 },
        { ...kLit, offset: 0.6 },
      ],
      { duration: kFlashMs, easing: "ease-out" },
    );
  },
  { capture: true, passive: true },
);
