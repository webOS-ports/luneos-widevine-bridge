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

// Wire format between the Chromium side CDM and the Android side bridge.
// Request: u8 op, u32 size, payload. Reply: i32 status, u32 size, payload.
#pragma once
#define BR_SOCK_HOST "/userdata/android-data/local/tmp/wv/br.sock"
#define BR_CLOCK_HOST "/userdata/android-data/local/tmp/wv/clock.sock"
#define BR_SOCK_DROID "/data/local/tmp/wv/br.sock"
#define BR_LOG_HOST "/userdata/android-data/local/tmp/wv/cdm.log"
enum {
    OP_CERT = 2,
    OP_OPEN = 1,
    OP_REQUEST = 3,
    OP_UPDATE = 4,
    OP_CLOSE = 5,
    OP_VINIT = 6,
    OP_AINIT = 7,
    OP_SAMPLE = 8,
    OP_RESET = 9
};
enum { ST_NOKEY = -11, ST_BUSY = -12 };
#include <stdint.h>
// Sample header, followed by nsub pairs of u32 clear/cipher sizes, then the data.
typedef struct {
    uint32_t video, scheme;
    int64_t ts;
    uint32_t size, nsub, crypt, skip;
    uint8_t key[16], iv[16];
} Smp;
typedef struct {
    uint32_t codec, a, b;
} Init;
