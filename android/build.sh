#!/bin/sh
# @@@LICENSE
#
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

# Builds the Android half with the NDK (API 29, aarch64).
# usage: android/build.sh /path/to/android-ndk [outdir]
set -e
NDK=${1:?usage: $0 /path/to/android-ndk [outdir]}
OUT=${2:-out}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang
cd "$(dirname "$0")/.."
mkdir -p "$OUT"
FLAGS="-O2 -Wall -Wextra"
$CC $FLAGS -I. android/wvbridged.c -lmediandk -o "$OUT/wvbridged"
# 2 ms: the shortest sleep that took the spinning thread's CPU down with no
# late frames in the tests.
$CC $FLAGS -fPIC -shared -DSLEEP_NS=2000000 android/wvshim.c -ldl -o "$OUT/libwvshim.so"
$CC $FLAGS android/wvlaunch.c -o "$OUT/wvlaunch"
