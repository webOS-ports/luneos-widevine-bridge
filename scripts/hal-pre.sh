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
# Takes the HAL from Android's init so the service can run it with the shim.
. /var/lib/widevine/bridge/common.sh
n=0
until [ "$(A "getprop init.svc.$HAL")" = running ]; do
    n=$((n + 1)); [ $n -gt 120 ] && { echo "wvbridge: HAL never started"; exit 1; }
    sleep 1
done
A "setprop ctl.stop $HAL"
n=0
until [ "$(A "getprop init.svc.$HAL")" = stopped ]; do
    n=$((n + 1)); [ $n -gt 10 ] && { echo "wvbridge: HAL did not stop"; exit 1; }
    sleep 1
done
A "grep -q lost+found /proc/mounts || mount --bind /data/local/tmp/wv /vendor/lost+found"
