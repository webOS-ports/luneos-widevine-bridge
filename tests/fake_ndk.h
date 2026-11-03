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

// What the tests can steer in, and read back from, the fake NDK.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    // steering
    int no_drm, no_codec, in_busy, reject_licence;
    size_t in_cap; // codec input buffer size; 0 means 256 KiB
    int audio_out; // output buffers the audio codec returns per input
    int32_t audio_out_size;
    void *image_buffer; // what AImage_getHardwareBuffer hands back
    // read back from the last queueSecureInputBuffer
    int queued, last_n, last_mode;
    size_t last_cl0, last_en0;
} FakeCtl;
extern FakeCtl fake;
