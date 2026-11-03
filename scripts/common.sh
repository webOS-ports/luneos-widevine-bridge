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
CDM=/var/lib/widevine/WidevineCdm/_platform_specific/linux_arm64/libwidevinecdm.so
HAL=vendor.drm-widevine-hal-1-4
A() { lxc-attach -n android -- /system/bin/sh -c "$1"; }
# Identifies the files the bridge replaces, without reading 900 MB at boot.
stamp() { stat -c '%s %Y %n' "$CBE" "$HWC" "$LSM" "$CDM"; }
