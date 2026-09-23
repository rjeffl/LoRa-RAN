#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
"""Run a CI job's steps on this machine, read from .github/workflows/ci.yml.

The `checks` job takes seconds and needs no toolchain, yet a citation that drifted or a
vector that no longer matches has so far been found by pushing and waiting for CI. This
runs the same commands before the push. It reads them from ci.yml rather than keeping a
list of its own, because ci.yml is the catalogue (root CLAUDE.md) and a second list
would drift from it.

    python3 tools/checks/run_ci_local.py              # the checks job
    python3 tools/checks/run_ci_local.py --job native # the host Unity suites too
    python3 tools/checks/run_ci_local.py --list       # print the steps, run nothing

Only `checks` and `native` are allowed. The firmware jobs copy secrets.h.example over
secrets.h, which in CI is a scratch file and here is your real one. For the same reason,
any step that names secrets.h is skipped rather than run, whichever job it is in.

Dependency installs (`pip install`) are skipped too: they set up a fresh runner, and
here they would change your Python environment. Steps call `python3`, which here
resolves to the interpreter running this script, so that interpreter needs what the
checks job installs (pyserial) and what this script reads ci.yml with (PyYAML).

Every step runs, even after a failure, so one run reports every failure. The exit
status is 1 when any step failed.
"""

import argparse
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
ALLOWED_JOBS = ("checks", "native")


def load_steps(job):
    try:
        import yaml
    except ImportError:
        sys.exit("FAIL: PyYAML is not installed. Run: python3 -m pip install pyyaml")
    workflow = yaml.safe_load(WORKFLOW.read_text())
    try:
        steps = workflow["jobs"][job]["steps"]
    except KeyError:
        sys.exit(f"FAIL: ci.yml has no job named {job!r}")
    runnable = []
    for step in steps:
        cmd = step.get("run")
        if not cmd:
            continue  # a `uses:` step: checkout, setup-python, cache
        name = step.get("name", cmd.splitlines()[0])
        if "pip install" in cmd:
            runnable.append((name, None, "installs dependencies"))
        elif "secrets.h" in cmd:
            runnable.append((name, None, "touches secrets.h"))
        else:
            runnable.append((name, cmd, None))
    return runnable


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--job", action="append", choices=ALLOWED_JOBS,
                        help="job to run; repeat for several (default: checks)")
    parser.add_argument("--list", action="store_true",
                        help="print the steps and exit without running them")
    args = parser.parse_args()
    jobs = args.job or ["checks"]
    if "native" in jobs and "checks" not in jobs:
        jobs.insert(0, "checks")

    # A step's `python3` is this interpreter, not whichever one PATH finds first, so
    # the dependencies to install are this interpreter's.
    env = dict(os.environ)
    env["PATH"] = os.path.dirname(sys.executable) + os.pathsep + env.get("PATH", "")
    python3 = pathlib.Path(sys.executable).with_name("python3")
    if not python3.exists():
        print(f"FAIL: no python3 beside {sys.executable}", file=sys.stderr)
        return 1

    failed = []
    for job in jobs:
        steps = load_steps(job)
        # CI runs --list on this script, so a ci.yml change that leaves it parsing
        # nothing fails there rather than reporting a quiet success here.
        if not any(cmd for _, cmd, _ in steps):
            print(f"FAIL: found no runnable steps in job {job!r}", file=sys.stderr)
            return 1
        print(f"== {job}")
        for name, cmd, skip in steps:
            if skip:
                print(f"  skip  {name} ({skip})")
                continue
            if args.list:
                print(f"  step  {name}")
                continue
            start = time.monotonic()
            # The shell CI uses for `run:` on ubuntu, from the repository root.
            result = subprocess.run(
                ["bash", "--noprofile", "--norc", "-eo", "pipefail", "-c", cmd],
                cwd=ROOT, env=env, capture_output=True, text=True)
            took = time.monotonic() - start
            if result.returncode == 0:
                print(f"  ok    {name} ({took:.1f} s)", flush=True)
            else:
                print(f"  FAIL  {name} ({took:.1f} s)")
                output = (result.stdout + result.stderr).rstrip()
                print("\n".join("        " + line for line in output.splitlines()))
                failed.append(f"{job}: {name}")

    if failed:
        print(f"\n{len(failed)} step(s) failed:")
        for name in failed:
            print(f"  {name}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
