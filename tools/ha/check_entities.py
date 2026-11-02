#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Every entity ID named in ha/packages/lran/ and ha/dashboards/ exists in the HA this is
# pointed at (HA Integration Implementation Plan 11, H9).
#
# WHY THIS EXISTS. HA does not reject an automation, template or card that names an entity it
# lacks. A trigger on a missing sensor never fires, states() returns "unknown", and a card
# shows "Entity not available". A renamed discovery row, or a template sensor whose name
# moved its entity ID, ships quietly and is found the day the alert should have sent. This
# is the check that falsifies "the YAML and the install agree" (root CLAUDE.md).
#
# It asks HA through the ha-mcp server, as the dev flow does (docs/ha/HANDOFF.md), not
# through the REST API: that needs a long-lived token, which only one machine had. The
# server's address carries its secret path, so it is read from ~/.claude.json and never
# printed.
#
#   check_entities.py                     against ha-sandbox
#   check_entities.py --server ha-prod    against production, at the port (plan 12)
#   check_entities.py --offline           list what the YAML names; needs no HA

import argparse
import json
import os
import re
import sys
import urllib.request

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCES = [os.path.join(ROOT, "ha", "packages", "lran"), os.path.join(ROOT, "ha", "dashboards")]

# HA's entity domains that could name an LRAN entity. A fixed list, not the live one, so a
# domain the install has nothing in at all (cover, before F1) is still checked.
DOMAINS = (
    "automation binary_sensor button counter cover event input_boolean input_button "
    "input_datetime input_number input_select input_text number person scene script select "
    "sensor switch text timer update zone"
).split()

# The lookbehind keeps trigger.event or states.sensor from reading as an ID. A trailing *
# is a glob (site.yaml.example's recorder filter) and is skipped.
_ID = re.compile(r"(?<![\w.])(" + "|".join(DOMAINS) + r")\.([a-z0-9_]+)(?![\w*])")


class _Loader(yaml.SafeLoader):
    pass


for tag in ("!include", "!secret", "!include_dir_named", "!input"):
    _Loader.add_constructor(tag, lambda loader, node: loader.construct_scalar(node))


def _strings(node, key=None):
    """Yield (key, string) for every string under node, keys included."""
    if isinstance(node, dict):
        for k, v in node.items():
            if isinstance(k, str):
                yield None, k
            yield from _strings(v, k)
    elif isinstance(node, list):
        for v in node:
            yield from _strings(v, key)
    elif isinstance(node, str):
        yield key, node


def named_ids():
    """Return ({entity_id: [file, ...]}, {prefix: [file, ...]}).

    A service name is not an entity: input_number.set_value under action: is skipped. A
    script's service is also its entity, so script.* stays, apart from script.turn_on, which
    is the domain's own. An ID ending in _ is a prefix a
    template completes ('input_select.lran_alert_' ~ alert); one live entity must match it.
    """
    ids, prefixes = {}, {}
    for src in SOURCES:
        for name in sorted(os.listdir(src)):
            # site.yaml.example is a template for site.yaml, which differs per install.
            if not name.endswith((".yaml", ".yml")) or name.endswith(".example"):
                continue
            rel = os.path.relpath(os.path.join(src, name), ROOT)
            with open(os.path.join(src, name), encoding="utf-8") as f:
                doc = yaml.load(f, Loader=_Loader) or {}
            pairs = list(_strings(doc))
            services = {s for k, s in pairs if k in ("action", "service", "perform_action")}
            for _, s in pairs:
                for m in _ID.finditer(s):
                    eid = m.group(0)
                    if eid in services and (m.group(1) != "script" or eid == "script.turn_on"):
                        continue
                    into = prefixes if eid.endswith("_") else ids
                    into.setdefault(eid, [])
                    if rel not in into[eid]:
                        into[eid].append(rel)
    return ids, prefixes


def _server_url(server):
    with open(os.path.expanduser("~/.claude.json"), encoding="utf-8") as f:
        servers = json.load(f).get("mcpServers", {})
    if server not in servers:
        sys.exit(f"no MCP server {server!r} in ~/.claude.json (have: {', '.join(servers) or 'none'})")
    return servers[server]["url"]


def _rpc(url, session, body):
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if session:
        headers["Mcp-Session-Id"] = session
    req = urllib.request.Request(url, json.dumps(body).encode(), headers)
    with urllib.request.urlopen(req, timeout=30) as r:
        session = r.headers.get("Mcp-Session-Id") or session
        text = r.read().decode()
    if "id" not in body:
        return session, None
    # A streamable-HTTP server may answer as server-sent events; the reply is a data: line.
    if not text.lstrip().startswith("{"):
        text = next(l[5:] for l in text.splitlines() if l.startswith("data:") and '"id"' in l)
    return session, json.loads(text)


def live_ids(server):
    """Every entity ID in the HA behind ha-mcp server, from one template render."""
    url = _server_url(server)
    session, _ = _rpc(url, None, {
        "jsonrpc": "2.0", "id": 1, "method": "initialize",
        "params": {"protocolVersion": "2025-03-26", "capabilities": {},
                   "clientInfo": {"name": "check_entities", "version": "1"}}})
    _rpc(url, session, {"jsonrpc": "2.0", "method": "notifications/initialized"})
    _, reply = _rpc(url, session, {
        "jsonrpc": "2.0", "id": 2, "method": "tools/call",
        "params": {"name": "ha_eval_template", "arguments": {
            "template": "{{ states | map(attribute='entity_id') | list | tojson }}"}}})
    if "error" in reply:
        sys.exit(f"{server}: {reply['error'].get('message')}")
    result = json.loads(reply["result"]["content"][0]["text"])
    if not result.get("success"):
        sys.exit(f"{server}: ha_eval_template failed: {result}")
    rendered = result["result"]
    return set(json.loads(rendered) if isinstance(rendered, str) else rendered)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--server", default="ha-sandbox", help="ha-mcp entry in ~/.claude.json")
    ap.add_argument("--offline", action="store_true", help="list the IDs the YAML names and stop")
    args = ap.parse_args()

    ids, prefixes = named_ids()
    if args.offline:
        for eid in sorted(ids) + sorted(p + "*" for p in prefixes):
            print(eid)
        print(f"ok: {len(ids)} entity IDs and {len(prefixes)} prefixes named", file=sys.stderr)
        return 0

    live = live_ids(args.server)
    errors = []
    for eid, files in sorted(ids.items()):
        if eid not in live:
            errors.append(f"{eid} not in {args.server} (named in {', '.join(files)})")
    for prefix, files in sorted(prefixes.items()):
        if not any(e.startswith(prefix) for e in live):
            errors.append(f"no {prefix}* entity in {args.server} (named in {', '.join(files)})")

    for e in errors:
        print(f"FAIL {e}")
    if errors:
        return 1
    print(f"ok: {len(ids)} entity IDs and {len(prefixes)} prefixes, all in {args.server} "
          f"({len(live)} entities)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
