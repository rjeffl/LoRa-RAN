# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# PlatformIO pre-script: stamps the commit that was built into LRAN_CAPTURE_GIT.
#
# CHAN-BOOT carries it into every capture file, so a file names the image that recorded
# it. A `-dirty` suffix means the running image matches no commit, and a capture made with
# one cannot be reproduced.
#
# The bridge's scripts/version.py, less the release number: this image has no releases.
# A script rather than a `!git describe` build flag for the bridge's reason - the shell
# quoting differs between the macOS build machine, the Kubuntu field laptop and CI.

import subprocess

Import("env")  # noqa: F821 - provided by PlatformIO's SCons environment

try:
    git = subprocess.check_output(
        ["git", "describe", "--always", "--dirty", "--abbrev=7"],
        cwd=env["PROJECT_DIR"],
        stderr=subprocess.DEVNULL,
    ).decode().strip()
except Exception:  # not a checkout, or no git on PATH
    git = "unknown"

env.Append(CPPDEFINES=[("LRAN_CAPTURE_GIT", env.StringifyMacro(git))])
