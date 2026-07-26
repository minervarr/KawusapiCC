#!/usr/bin/env bash
# kobuzapi standalone build (Linux) — smoke-builds core/ (kobuzapi_core +
# the archive_engine modules it needs: ae_util, ae_net, ae_tag).
#
# core/net's CMakeLists.txt falls back to find_package(CURL) on desktop, so
# a system libcurl (Arch: pacman -S curl; Debian: libcurl4-openssl-dev) is
# enough for ae_net. core/tag has no such system fallback (TagLib ships no
# CMake find-module) — either install a TagLib that provides a `tag` CMake
# target (taglib's own CMake install does), or point TAGLIB_DIR at one, or
# just accept that this script's smoke build stops at ae_tag; a real
# consumer (e.g. streamer) vendors its own TagLib and add_subdirectory()s
# this repo's core/ after it, at which point ae_tag links fine — see
# streamer's root CMakeLists.txt for the reference setup.
set -e

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/build"

cmake -S "$root/core" -B "$build" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build" -j"$(nproc)"

echo
echo "Done: $build (kobuzapi_core, ae_util, ae_net, ae_tag)"
