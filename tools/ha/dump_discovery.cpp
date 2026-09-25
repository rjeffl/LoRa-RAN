// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Writes every discovery config the bridge would publish, to a directory, one file per
// topic. Task BF-23; Impl Plan 4.4 asks for these to be committed under /ha/.
//
// WHY IT IS BUILT FROM THE FIRMWARE'S OWN SOURCES rather than written by hand or
// captured from a broker. A hand-written example drifts from the code the first time a
// table row changes, and drifts silently, because nothing reads it. This links
// discovery.cpp itself, so an example that disagrees with the firmware cannot be
// produced - and tools/checks/ha_examples.py fails the build when the committed files
// stop matching what this prints.
//
// It also means the examples exist without a board, a broker or a Home Assistant
// install, which is the other half of what Impl Plan 4.4 wants them for.

#include <cstdio>
#include <cstring>
#include <string>

#include "discovery.h"
#include "registry.h"

using namespace bridge;

namespace {

// The provisioned fleet, exactly as registry.cpp derives it from kNodeTable.
void fill(NodeInfo* out) {
  for (size_t i = 0; i < kNodeCount; ++i) {
    out[i].id       = kNodeTable[i].id;
    out[i].type     = kNodeTable[i].type;
    out[i].is_bench = lran::is_bench_node(kNodeTable[i].id);
  }
}

// `homeassistant/sensor/lran_bridge_version/config` -> `sensor-lran_bridge_version.json`.
// A filename cannot hold the slashes and the prefix repeats on every row.
std::string filename(const char* topic) {
  std::string s(topic);
  const std::string prefix = std::string(kDiscoveryPrefix) + "/";
  if (s.rfind(prefix, 0) == 0) s = s.substr(prefix.size());
  const std::string suffix = "/config";
  if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(),
                                            suffix) == 0) {
    s = s.substr(0, s.size() - suffix.size());
  }
  for (char& c : s) {
    if (c == '/') c = '-';
  }
  return s + ".json";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: dump_discovery <out-dir> [--bench]\n");
    return 2;
  }
  const std::string dir(argv[1]);
  // Off by default, matching the firmware: BF-26 owns the toggle, and the committed
  // examples describe what a bridge publishes today.
  bool bench = false;
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--bench") == 0) bench = true;
  }

  NodeInfo fleet[kNodeCount];
  fill(fleet);

  DiscoveryCursor cur;
  DiscoveryItem   item;
  size_t          n = 0;
  while (discovery_next(&cur, fleet, kNodeCount, bench, &item)) {
    char topic[128];
    char config[kMaxDiscoveryPayload];
    if (discovery_topic(item, topic, sizeof(topic)) == 0 ||
        discovery_config_json(item, config, sizeof(config)) == 0) {
      std::fprintf(stderr, "ERR entity %s produced nothing\n", discovery_object_id(item));
      return 1;
    }
    const std::string path = dir + "/" + filename(topic);
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
      std::fprintf(stderr, "ERR cannot write %s\n", path.c_str());
      return 1;
    }
    // The topic is not in the document, and it is half of what an example has to show.
    std::fprintf(f, "%s\n", config);
    std::fclose(f);
    std::printf("%s\n", topic);
    ++n;
  }
  std::fprintf(stderr, "%zu configs\n", n);
  return 0;
}
