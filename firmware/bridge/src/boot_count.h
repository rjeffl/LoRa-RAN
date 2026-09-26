// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge's boot count. Spec 16.3, D67.
//
// A bridge event's event_id restarts at 1 after every boot, so event_id alone repeats and
// Home Assistant would drop a new event as a retransmission of an old one. The boot count
// is the other half of the key. It costs one NVS write per boot, where persisting the
// event counter would have cost one per event.
//
// ITS OWN NAMESPACE, NOT THE CONFIGURATION'S. A restore_defaults cleared `cfg` whole until
// 2026-09-26, and now removes the table's keys one at a time (nvs_persist.h). A counter in
// its own namespace depends on neither behavior.

#pragma once

#include <cstdint>

namespace bridge {

// Reads the stored count, adds one, writes it back and returns the new value, so the
// first boot is 1. Returns 0 when NVS could not open or write, which the event documents
// publish as `"boot": null` rather than as a number that would repeat.
uint32_t boot_count_advance();

}  // namespace bridge
