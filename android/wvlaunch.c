/* @@@LICENSE
 *
 * Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

// Starts the Widevine HAL as init would (user, groups), with the shim preloaded.
//
// Where the shim lives depends on how the bridge was installed: by hand from the
// NDK build, bind-mounted onto /vendor/lost+found, or built into a Halium GSI
// (Android.bp), in /system/lib64, found by name through the link Halium's
// linkerconfig adds from the system namespace into the vendor one.
#ifndef WV_SHIM
#define WV_SHIM "/vendor/lost+found/libwvshim.so"
#endif
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    gid_t g[] = {1000, 1026, 1031};
    if (setgroups(3, g) || setgid(1013) || setuid(1013)) {
        perror("drop");
        return 1;
    }
    setenv("LD_PRELOAD", WV_SHIM, 1);
    execl("/vendor/bin/hw/android.hardware.drm@1.4-service.widevine",
          "android.hardware.drm@1.4-service.widevine", (char *)0);
    perror("exec");
    return 1;
}
