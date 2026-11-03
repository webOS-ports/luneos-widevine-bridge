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
# Paths for the bridge services.
B=/var/lib/widevine/bridge
W=/userdata/android-data/local/tmp/wv
CBE=/usr/lib/libcbe.so
HWC=/usr/lib/plugins/platforms/libhwcomposer.so
LSM=/usr/lib/libWebOSCoreCompositor.so.0.0.1
# The bridge CDM sits next to Google's, which stays in place for every site
# the bridge does not list (see sites).
BCDM=/var/lib/widevine/BridgeCdm
HAL=vendor.drm-widevine-hal-1-4
A() { lxc-attach -n android -- /system/bin/sh -c "$1"; }
# A Halium GSI built with the bridge (Android.bp) carries the Android half in
# /system, which LuneOS sees under /android. Without it, the NDK build in $W
# is used, with the shim bind-mounted onto /vendor/lost+found.
gsi() {
    [ -x /android/system/bin/wvbridged ] && [ -x /android/system/bin/wvlaunch ] &&
        [ -f /android/system/lib64/libwvshim.so ]
}
# Identifies the files the bridge replaces, without reading 900 MB at boot.
stamp() { stat -c '%s %Y %n' "$CBE" "$HWC" "$LSM"; }
