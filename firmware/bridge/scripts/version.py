# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# PlatformIO pre-script: stamps the bridge's version into the build. Task BF-13;
# R-5.3e, V-B9.
#
# Two values, because they answer different questions. LRAN_BRIDGE_VERSION is the
# release a person chose (`custom_bridge_version` in platformio.ini). LRAN_BRIDGE_GIT
# is the commit that was actually built - and after a rollback, it is the field that
# proves which image is running, because two builds of one release differ only here.
#
# A script rather than a `!git describe` build flag: the shell quoting that approach
# needs differs between the macOS build machine, the Kubuntu field laptop and CI, and
# a version string that is right on two of three is the kind nobody checks.

import os
import subprocess

Import("env")  # noqa: F821 - provided by PlatformIO's SCons environment

version = env.GetProjectOption("custom_bridge_version", "0.0.0")

try:
    git = subprocess.check_output(
        ["git", "describe", "--always", "--dirty", "--abbrev=7"],
        cwd=env["PROJECT_DIR"],
        stderr=subprocess.DEVNULL,
    ).decode().strip()
except Exception:  # not a checkout, or no git on PATH
    git = "unknown"

env.Append(CPPDEFINES=[("LRAN_BRIDGE_VERSION", env.StringifyMacro(version))])

# LRAN_BRIDGE_GIT changes with every commit, and a define is part of every object's
# build signature. Defined project-wide, it made each commit recompile the whole
# framework and defeated CI's build cache, so only the files that print it get it.
GIT_USERS = {os.path.join(env.subst("$PROJECT_SRC_DIR"), f) for f in ("main.cpp", "ota.cpp")}


def stamp_git(env, node):
    if node.srcnode().get_abspath() not in GIT_USERS:
        return node
    stamped = env.Clone()
    stamped.Append(CPPDEFINES=[("LRAN_BRIDGE_GIT", env.StringifyMacro(git))])
    return stamped.Object(node)


env.AddBuildMiddleware(stamp_git)
