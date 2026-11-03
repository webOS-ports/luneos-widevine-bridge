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
# Binds the bridge's libraries and allows the video plane, or leaves stock.
. /var/lib/widevine/bridge/common.sh
if [ "$(stamp)" != "$(cat $B/base.stamp 2>/dev/null)" ]; then
    echo "wvbridge: system files differ from the install, staying stock"
    exit 0
fi
for f in $B/libcbe-br.so $B/libhwcomposer.so $B/libWebOSCoreCompositor.so.0.0.1 \
         $W/libwidevinecdm.so $W/wvbridged $W/wvlaunch $W/libwvshim.so; do
    [ -f "$f" ] || { echo "wvbridge: $f missing, staying stock"; exit 0; }
done
if mount --bind $B/libcbe-br.so $CBE &&
   mount --bind $B/libhwcomposer.so $HWC &&
   mount --bind $B/libWebOSCoreCompositor.so.0.0.1 $LSM &&
   mount --bind $W/libwidevinecdm.so $CDM; then
    mkdir -p /run/wvbridge
    rm -f $W/ov.sock
    : > $W/cdm.log; chmod 666 $W/cdm.log
    echo "QPA_HWC_VIDEO_SOCK=$W/ov.sock" > /run/wvbridge/env
    exit 0
fi
echo "wvbridge: a mount failed, staying stock"
exec $B/libs-down.sh
