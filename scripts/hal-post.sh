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
# Always puts Android's own HAL back, so DRM keeps working without the shim.
. /var/lib/widevine/bridge/common.sh
for p in $(ps -eo pid,args | grep "[d]rm@1.4-service.widevine" | grep -v "lxc-attach\|/bin/sh" | /usr/bin/awk '{print $1}'); do
    kill -9 "$p"
done
pkill -9 -x wvlaunch 2>/dev/null
A "umount /vendor/lost+found" 2>/dev/null
A "setprop ctl.start $HAL"
# wvbridged holds a dead HAL connection now: every openSession would fail.
/bin/systemctl --no-block try-restart wvbridged.service
exit 0
