#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# The interleaved spacing sweep: alternate two inter-frame gaps inside ONE session and
# pool the frames both by arm and by when they were sent. The arithmetic is
# sweep_analyze.py, which does no I/O; this is the half that needs two boards and a
# broker.
#
# WHAT THIS ANSWERS. Whether the bridge's receive losses are a function of frame spacing
# at all. Every sweep on record - M22's morning curve, the afternoon frame-log session -
# ran one spacing to completion before starting the next, so a slow change in the
# environment and an effect of spacing produce the same table. The two sessions
# disagreed: 5.6 % at 250 ms and 0 % at 2000 ms in the morning, 2.50 % and 1.90 % the
# same afternoon, with every loss falling in four of ten runs whatever the spacing.
# Alternating the arms is what tells the two apart. Impl Plan 8.1, M22, V-B12.
#
# WHY THE FRAME LOG AND NOT THE COUNTERS. per_measure.py brackets each burst with two
# diagnostic publications, which cost up to two minutes a burst at the 60 s interval and
# reboot the sender on every invocation. BF-27's frame log publishes as frames arrive,
# so a burst costs its own airtime, the port stays open for the whole sweep, and each
# loss comes with the record either side of it. A burst is then about 45 s at 2000 ms
# and about 15 s at 250 ms.
#
# THE SENDER'S OWN COUNT IS THE DENOMINATOR. `sent` is the TX_DONE delta from the
# flooding board's driver, not the number this tool asked for: a frame the radio never
# put on air is not a frame the bridge failed to hear.
#
# BOTH BOARDS ARE QUIETED AND BOTH PORTS STAY OPEN. A disabled identity re-enables
# itself on the next boot and opening a port reboots the board, so quieting has to
# happen in the session that runs the measurement (traps.md, bench boards).
#
# CREDENTIALS COME FROM THE ENVIRONMENT AND NEVER FROM argv, like every other tool here:
# LRAN_MQTT_HOST, LRAN_MQTT_USER, LRAN_MQTT_PASSWORD. An argument reaches argv, and argv
# reaches the process table and the shell history.
#
#   export LRAN_MQTT_HOST=... LRAN_MQTT_USER=...
#   read -rs LRAN_MQTT_PASSWORD && export LRAN_MQTT_PASSWORD
#   python3 tools/simctl/sweep_interleave.py --port /dev/cu.usbmodem1101 \
#       --quiet-port /dev/cu.usbserial-3 --pairs 6 --json sweep.json
#   python3 tools/simctl/sweep_interleave.py --read sweep.json    # no board, re-read
#
# V-B12'S SATURATED ARM. With --bridge-port and --blast-kbps, the two arms are one gap
# with the bridge's WiFi idle and the same gap with it loaded. The bridge must run the
# `v_b12_blaster` image (firmware/bridge/src/blaster.h). Its port is opened once and
# held: opening it reboots the bridge and zeroes its counters. Each loaded burst records
# what the blaster actually sent, because the rate asked for is not the load applied.
#
#   python3 tools/simctl/sweep_interleave.py --port /dev/cu.usbmodem2101 \
#       --quiet-port /dev/cu.usbserial-4 --bridge-port /dev/cu.usbserial-0001 \
#       --gaps 2000 --blast-kbps 4000 --pairs 6 --json vb12.json

import argparse
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from per_measure import sender_tx_frames
from rxlog import RxLogSubscriber
from rxlog_analyze import merge
from simctl import Console
from sweep_analyze import burst_from, format_report

VERSION_TOPIC = "lran/bridge/version"


def arm_name(gap_ms, blast_kbps=0):
    if blast_kbps:
        return "blast%d" % blast_kbps
    return "gap%d" % gap_ms


def parse_blast(line):
    """The blaster's `blast: off sent=.. bytes=..` line as a dict, or None.

    The fields are what the stack accepted, not what was asked for: `fail` counts the
    sends it refused, and `kbps` is the achieved rate over `ms`.
    """
    parts = line.split()
    if len(parts) < 2 or parts[0] != "blast:" or parts[1] not in ("on", "off"):
        return None
    out = {"state": parts[1]}
    for part in parts[2:]:
        key, sep, value = part.partition("=")
        if sep and value.isdigit():
            out[key] = int(value)
    return out if "sent" in out else None


def blast_command(bridge, line, wait_s=1.0):
    """Send one blaster command and return its totals line, parsed, or None."""
    mark = time.time()
    bridge.send(line)
    end = mark + wait_s
    while time.time() < end:
        for text in bridge.since(mark):
            parsed = parse_blast(text)
            if parsed:
                return parsed
        time.sleep(0.1)
    return None


def schedule(gaps, pairs):
    """The order the arms run in: alternating, and swapping which one leads each pair.

    A plain A,B,A,B rotation puts every A before its own B, so an effect that decays
    through a pair would land entirely on B. Swapping the lead each pair spreads that
    across both arms and costs nothing.
    """
    a, b = gaps
    out = []
    for i in range(pairs):
        out.extend([a, b] if i % 2 == 0 else [b, a])
    return out


def identities(console):
    """Every identity the board holds, as lower-case hex, from `id list`."""
    mark = time.time()
    console.send("id list")
    time.sleep(0.8)
    found = []
    for text in console.since(mark):
        parts = text.split()
        if len(parts) >= 2 and parts[0] == "id":
            found.append(parts[1].lower())
    return sorted(set(found))


def quiet_board(console, keep, out):
    """Disable every identity but `keep`. Pass keep=None to silence the board outright.

    An identity left enabled answers POLLs, and every answer is a frame the bridge logs
    from a peer this sweep is not measuring. per_window.py's guard catches the result,
    but catching it after a 20-minute run is worse than preventing it.
    """
    for node in identities(console):
        if keep is not None and node == keep.lower():
            continue
        console.send("disable " + node)
        print("  disabled %s" % node, file=out)


def ensure_flood_identity(console, node, out):
    """Add the flooding identity in ROLE_FAULT when the board does not hold it.

    ROLE_FAULT answers no POLL (simnode node.cpp), so the only frames this identity
    sends are the burst. 0xF0-0xF3 are in the bridge's registry already (registry.h),
    so the frames pass the ladder rather than counting as rx_unknown_src.
    """
    if node.lower() in identities(console):
        return
    console.send("id add %s ROLE_FAULT" % node)
    time.sleep(0.5)
    print("  added %s in ROLE_FAULT" % node, file=out)


def run_burst(console, sub, node, count, gap_ms, settle_s, drain_s, out,
              bridge=None, blast_kbps=0, blast_bytes=1472, warmup_s=2.0):
    """One burst, bracketed by the bridge's record index and the sender's TX_DONE count.

    With blast_kbps, the bridge's blaster runs from before the first frame to after the
    last. It stops before the drain, so the last frame-log batches travel an idle link.
    """
    arm = arm_name(gap_ms, blast_kbps)
    if blast_kbps:
        bridge.send("blast %d %d" % (blast_kbps, blast_bytes))
        time.sleep(warmup_s)
    records, _ring, _transport = merge(sub.snapshot())
    i_before = records[-1]["i"] if records else None
    tx_before = sender_tx_frames(console)
    if tx_before is None:
        if blast_kbps:
            blast_command(bridge, "blast 0")
        return {"arm": arm, "gap_ms": gap_ms, "count": count, "sent": 0,
                "i_before": i_before, "i_after": i_before,
                "refused": "the simnode did not answer `radio`"}

    t_start = time.time()
    console.send("fault %s flood %d gap %d" % (node, count, gap_ms))

    # Wait for the burst to finish rather than predicting its length. Each frame does a
    # CAD and may back off (backoff_max_ms 1500, D1), so the floor is
    # count * (gap + airtime) and the ceiling is well above it.
    deadline = t_start + settle_s + count * (gap_ms / 1000.0 + 2.0)
    last, stable_since = tx_before, None
    while time.time() < deadline:
        time.sleep(1.0)
        now = sender_tx_frames(console)
        if now is None:
            continue
        if now != last:
            last, stable_since = now, None
            continue
        if stable_since is None:
            stable_since = time.time()
        elif time.time() - stable_since >= settle_s and last > tx_before:
            break

    blast = None
    if blast_kbps:
        blast = blast_command(bridge, "blast 0")
        if blast is None:
            print("  the blaster did not report its totals", file=out)
        else:
            print("  blaster: %d packets, %d kbps achieved, %d refused by the stack,"
                  " %d polls with the link down"
                  % (blast.get("sent", 0), blast.get("kbps", 0), blast.get("fail", 0),
                     blast.get("down", 0)), file=out)

    # The last frames of a burst are still in flight when TX_DONE stops moving, and a
    # record that arrives after i_after is read counts as a loss here and as excess
    # traffic in the next burst.
    time.sleep(drain_s)
    records, _ring, _transport = merge(sub.snapshot())
    i_after = records[-1]["i"] if records else None

    mark = {"arm": arm, "gap_ms": gap_ms, "count": count,
            "sent": last - tx_before, "i_before": i_before, "i_after": i_after,
            "t_start": t_start, "t_end": time.time()}
    if blast_kbps:
        mark["blast_kbps"] = blast_kbps
        mark["blast"] = blast
    return mark


def host_git():
    """The commit of the tree this tool ran from. NOT the image on the bridge.

    The two are different things and a capture that records one field called `git`
    invites a reader to take it for the other. The bridge's own image is read from the
    broker instead, and a reflash mid-session would show there and nowhere else.
    """
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                       stderr=subprocess.DEVNULL).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def bridge_version(host, port, user, password, wait_s=5.0):
    """The bridge's running image, from its retained `lran/bridge/version`.

    Provenance a reader cannot get any other way: which firmware lost these frames.
    Returns None when the topic does not arrive, which is a bridge that has not
    published since the broker last restarted rather than a bridge that is absent.
    """
    import paho.mqtt.client as mqtt

    seen = []
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if user:
        c.username_pw_set(user, password)
    c.on_connect = lambda cl, u, f, rc, props=None: cl.subscribe([(VERSION_TOPIC, 1)])
    c.on_message = lambda cl, u, msg: seen.append(msg.payload.decode("utf-8", "replace"))
    try:
        c.connect(host, port, 30)
        c.loop_start()
        end = time.time() + wait_s
        while time.time() < end and not seen:
            time.sleep(0.2)
    finally:
        c.loop_stop()
        c.disconnect()
    if not seen:
        return None
    try:
        return json.loads(seen[0])
    except ValueError:
        return None


def report(capture, out):
    """Everything the run measured, from a capture in memory or read back from disk."""
    records, ring_lost, transport_lost = merge(capture["batches"])
    peer = int(capture["peer"], 16) if isinstance(capture["peer"], str) else capture["peer"]

    if ring_lost:
        print("RING OVERWROTE %d record(s) - the bridge's log_task fell behind, so an"
              " arrival may be missing from the log rather than from the air."
              % ring_lost, file=out)
    if transport_lost:
        print("%d record(s) left the bridge and did not reach this subscriber. QoS 0, a"
              " broker restart, or a late start - not the receive path." % transport_lost,
              file=out)

    from sweep_analyze import slice_records
    bursts = []
    for n, mark in enumerate(capture["marks"], start=1):
        mark = dict(mark, n=n)
        if mark.get("refused"):
            b = burst_from(dict(mark, sent=0), [], peer)
            b.invalid_reason = mark["refused"]
            bursts.append(b)
            continue
        bursts.append(burst_from(mark, slice_records(records, mark), peer))
    print(format_report(bursts, peer), file=out)

    loaded = [m for m in capture["marks"] if m.get("blast_kbps")]
    if loaded:
        print(format_blast(loaded), file=out)


def format_blast(marks):
    """What the blaster achieved in each loaded burst. A low `kbps` beside a low PER is a
    load that never arrived, not a receiver that tolerated it."""
    lines = ["", "Blaster, per loaded burst (asked / achieved kbps, packets, refused,"
             " link-down polls):"]
    for n, m in enumerate(marks, start=1):
        blast = m.get("blast")
        if not blast:
            lines.append("  %2d  %6d / (no totals reported)" % (n, m["blast_kbps"]))
            continue
        lines.append("  %2d  %6d / %6d  %7d  %5d  %5d"
                     % (n, m["blast_kbps"], blast.get("kbps", 0), blast.get("sent", 0),
                        blast.get("fail", 0), blast.get("down", 0)))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="the interleaved spacing sweep - is the loss rate a function of gap?")
    ap.add_argument("--port", help="the flooding simnode's serial port")
    ap.add_argument("--quiet-port", help="a second simnode to silence for the run")
    ap.add_argument("--node", default="f3", help="identity to flood from (default f3)")
    ap.add_argument("--count", type=int, default=40, help="frames per burst")
    ap.add_argument("--gaps", default="250,2000",
                    help="the two gaps, in ms; one gap with --blast-kbps")
    ap.add_argument("--bridge-port",
                    help="the bridge's serial port, held open for the run (V-B12)")
    ap.add_argument("--blast-kbps", type=int, default=0,
                    help="the loaded arm's blaster rate; the other arm is idle (V-B12)")
    ap.add_argument("--blast-bytes", type=int, default=1472,
                    help="the blaster's UDP payload size")
    ap.add_argument("--bridge-boot", type=float, default=25.0,
                    help="seconds to let the bridge reach its broker after its port opens")
    ap.add_argument("--pairs", type=int, default=6, help="how many times to run both arms")
    ap.add_argument("--settle", type=float, default=4.0,
                    help="seconds TX_DONE must hold still before a burst counts as done")
    ap.add_argument("--drain", type=float, default=6.0,
                    help="seconds to let the last frame-log batches arrive")
    ap.add_argument("--json", help="write the run to this file")
    ap.add_argument("--read", help="re-read a saved run instead of driving one")
    ap.add_argument("--host", default=os.environ.get("LRAN_MQTT_HOST", "localhost"))
    ap.add_argument("--mqtt-port", type=int, default=1883)
    args = ap.parse_args(argv)

    if args.read:
        with open(args.read) as fh:
            report(json.load(fh), sys.stdout)
        return 0

    if not args.port:
        ap.error("--port is required unless --read is given")

    gaps = [int(g) for g in args.gaps.split(",")]
    if args.blast_kbps:
        if not args.bridge_port:
            ap.error("--blast-kbps needs --bridge-port")
        if len(gaps) != 1:
            ap.error("--gaps takes one value with --blast-kbps; the arms differ in load only")
        arms = [(gaps[0], 0), (gaps[0], args.blast_kbps)]
    else:
        if len(gaps) != 2:
            ap.error("--gaps takes exactly two values")
        arms = [(g, 0) for g in gaps]

    # First, because opening this port reboots the bridge. The broker connection and the
    # retained version below both have to come after that boot, not before it.
    bridge = Console(args.bridge_port) if args.bridge_port else None
    if bridge:
        print("bridge port open - waiting %.0f s for it to boot and reach the broker"
              % args.bridge_boot)
        time.sleep(args.bridge_boot)

    user = os.environ.get("LRAN_MQTT_USER")
    password = os.environ.get("LRAN_MQTT_PASSWORD")
    image = bridge_version(args.host, args.mqtt_port, user, password)
    print("bridge image: %s" % (json.dumps(image) if image else "NOT PUBLISHED"))

    sub = RxLogSubscriber(args.host, args.mqtt_port, user, password)

    console = Console(args.port)
    quiet = Console(args.quiet_port) if args.quiet_port else None
    # Opening a port reboots the board, and a rebooted simnode starts with its stored
    # identities enabled again.
    time.sleep(8.0)

    try:
        if quiet:
            print("quieting %s:" % args.quiet_port)
            quiet_board(quiet, None, sys.stdout)
        print("quieting %s, keeping %s:" % (args.port, args.node))
        ensure_flood_identity(console, args.node, sys.stdout)
        quiet_board(console, args.node, sys.stdout)

        if bridge and args.blast_kbps and blast_command(bridge, "blast") is None:
            raise SystemExit("the bridge did not answer `blast` - is it running the"
                             " v_b12_blaster image?")

        order = schedule(arms, args.pairs)
        print("\n%d bursts of %d frames: %s\n"
              % (len(order), args.count, " ".join(arm_name(*a) for a in order)))

        marks = []
        for n, (gap_ms, kbps) in enumerate(order, start=1):
            print("burst %d/%d - %s" % (n, len(order), arm_name(gap_ms, kbps)))
            mark = run_burst(console, sub, args.node, args.count, gap_ms,
                             args.settle, args.drain, sys.stdout,
                             bridge=bridge, blast_kbps=kbps, blast_bytes=args.blast_bytes)
            marks.append(mark)
            print("  sent %d, records %s to %s"
                  % (mark["sent"], mark["i_before"], mark["i_after"]))

        capture = {"tool": "sweep_interleave", "host_git": host_git(),
                   "bridge_image": image,
                   "started": marks[0].get("t_start") if marks else None,
                   "peer": "0x%s" % args.node.lower(), "count": args.count,
                   "gaps": gaps, "pairs": args.pairs,
                   "blast_kbps": args.blast_kbps, "blast_bytes": args.blast_bytes,
                   "marks": marks, "batches": sub.snapshot()}
    finally:
        console.close()
        if quiet:
            quiet.close()
        if bridge:
            # A blaster left running would load the network after the run ends. Closing
            # the port does not reboot the bridge, so the command has to be sent.
            bridge.send("blast 0")
            bridge.close()
        sub.close()

    if args.json:
        with open(args.json, "w") as fh:
            json.dump(capture, fh, indent=1, sort_keys=True)
        print("\nwrote %s" % args.json)

    print()
    report(capture, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
