#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# ha/packages/lran/messages.yml against the packages that use it (HA Integration
# Implementation Plan 7.5).
#
# WHY THIS EXISTS. script.lran_notify never withholds an alert for want of its text: a name
# with no catalog entry sends under the bare name, and an alert with no selector falls back
# to the caller's severity. That is the right failure for a gate, and it is also silent. A
# renamed entry or a selector added without its level would ship and be found on the phone.
# This is the check that falsifies "every alert has its words and its selector" (root
# CLAUDE.md: a load-bearing premise names the check that would falsify it).
#
# It reads the YAML statically; it needs no running HA.

import os
import re
import sys

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PKG = os.path.join(ROOT, "ha", "packages", "lran")
LEVELS = {"Off", "Low", "Normal", "High", "Critical"}


class _Loader(yaml.SafeLoader):
    pass


# HA's own tags. Their targets are not needed here, only the files' structure.
for tag in ("!include", "!secret", "!include_dir_named", "!input"):
    _Loader.add_constructor(tag, lambda loader, node: loader.construct_scalar(node))


def _load(path):
    with open(path, encoding="utf-8") as f:
        return yaml.load(f, Loader=_Loader) or {}


def _notify_calls(node):
    """Yield the data block of every script.lran_notify action under node."""
    if isinstance(node, dict):
        if node.get("action") == "script.lran_notify" and isinstance(node.get("data"), dict):
            yield node["data"]
        for v in node.values():
            yield from _notify_calls(v)
    elif isinstance(node, list):
        for v in node:
            yield from _notify_calls(v)


def main():
    errors = []
    catalog = _load(os.path.join(PKG, "messages.yml"))
    packages = {
        name: _load(os.path.join(PKG, name))
        for name in sorted(os.listdir(PKG))
        if name.endswith(".yaml")
    }

    selectors = set()
    for name, pkg in packages.items():
        for key, sel in (pkg.get("input_select") or {}).items():
            if not key.startswith("lran_alert_"):
                continue
            selectors.add(key[len("lran_alert_"):])
            if set(sel.get("options", [])) != LEVELS:
                errors.append(f"{name}: input_select.{key} options are not {sorted(LEVELS)}")

    for key, entry in catalog.items():
        if key.startswith("_"):
            continue
        if not isinstance(entry, dict) or "title" not in entry or "message" not in entry:
            errors.append(f"messages.yml: {key} needs a title and a message")
            continue
        if "level" in entry:
            # A bare Off loads as False, which no selector option matches.
            if entry["level"] not in LEVELS:
                errors.append(f"messages.yml: {key} level {entry['level']!r} is not one of "
                              f"{sorted(LEVELS)} (write \"Off\" in quotes)")
            if key not in selectors:
                errors.append(f"messages.yml: {key} has a level but no input_select.lran_alert_{key}")
    for sel in sorted(selectors):
        if not isinstance(catalog.get(sel), dict) or "level" not in catalog[sel]:
            errors.append(f"input_select.lran_alert_{sel} has no messages.yml entry with a level")

    for name, pkg in packages.items():
        for data in _notify_calls(pkg.get("automation") or []):
            alert, msg = data.get("alert"), data.get("msg")
            text = msg or alert
            if text is None:
                continue  # a caller passing its own title and message
            entry = catalog.get(text)
            if not isinstance(entry, dict):
                errors.append(f"{name}: lran_notify names {text!r}, which messages.yml lacks")
                continue
            if alert is not None and alert not in selectors:
                errors.append(f"{name}: lran_notify alert {alert!r} has no selector")
            passed = set((data.get("fields") or {}).keys())
            wanted = set(re.findall(r"\{(\w+)\}", f"{entry['title']} {entry['message']}"))
            for missing in sorted(wanted - passed):
                errors.append(f"{name}: {text} uses {{{missing}}}, which the call does not pass")

    # A misspelt name here would quietly leave the additional recipients off that alert.
    notify = packages["core.yaml"]["script"]["lran_notify"]
    for name in notify["variables"].get("additional_alerts", []):
        if name not in selectors:
            errors.append(f"core.yaml: additional_alerts names {name!r}, which has no selector")

    for e in errors:
        print(f"FAIL {e}")
    if errors:
        return 1
    print(f"ok: {len(catalog)} catalog entries, {len(selectors)} alert selectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
