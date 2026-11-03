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

// Host stand-ins for the NDK media calls wvbridged makes, so its protocol and
// parsing code can run on x86-64 under the sanitisers and valgrind. Objects
// are real allocations, so a missing delete shows up as a leak.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaCrypto.h>
#include <media/NdkMediaDrm.h>
#include "fake_ndk.h"

struct AMediaDrm {
    int x;
};
struct AMediaCrypto {
    int x;
};
struct AMediaCodec {
    int audio;
    atomic_int out_left; // output buffers still to hand out
};
struct AMediaFormat {
    int x;
};
struct AMediaCodecCryptoInfo {
    int n;
    cryptoinfo_mode_t mode;
    size_t *cl, *en;
};
struct AImageReader {
    int x;
};
struct AImage {
    int x;
};
struct ANativeWindow {
    int x;
};

const char *AMEDIAFORMAT_KEY_MIME = "mime";
const char *AMEDIAFORMAT_KEY_WIDTH = "width";
const char *AMEDIAFORMAT_KEY_HEIGHT = "height";
const char *AMEDIAFORMAT_KEY_MAX_INPUT_SIZE = "max-input-size";
const char *AMEDIAFORMAT_KEY_SAMPLE_RATE = "sample-rate";
const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT = "channel-count";
const char *AMEDIAFORMAT_KEY_IS_ADTS = "is-adts";

FakeCtl fake;

static uint8_t inbuf[256 * 1024];
static uint8_t outbuf[4096];

AMediaDrm *AMediaDrm_createByUUID(const uint8_t *uuid) {
    (void)uuid;
    return fake.no_drm ? NULL : calloc(1, sizeof(AMediaDrm));
}
void AMediaDrm_release(AMediaDrm *d) {
    free(d);
}
media_status_t AMediaDrm_setPropertyByteArray(AMediaDrm *d, const char *n, const uint8_t *v,
                                              size_t l) {
    (void)d, (void)n, (void)v, (void)l;
    return AMEDIA_OK;
}
media_status_t AMediaDrm_setPropertyString(AMediaDrm *d, const char *n, const char *v) {
    (void)d, (void)n, (void)v;
    return AMEDIA_OK;
}
media_status_t AMediaDrm_openSession(AMediaDrm *d, AMediaDrmSessionId *s) {
    (void)d;
    static atomic_uint next;
    uint8_t *p = malloc(8);
    if (!p) return AMEDIA_ERROR_UNKNOWN;
    unsigned v = atomic_fetch_add(&next, 1) + 1;
    memset(p, 0, 8);
    memcpy(p, &v, sizeof v);
    s->ptr = p;
    s->length = 8;
    return AMEDIA_OK;
}
media_status_t AMediaDrm_closeSession(AMediaDrm *d, const AMediaDrmSessionId *s) {
    (void)d;
    free((void *)s->ptr);
    return AMEDIA_OK;
}
media_status_t AMediaDrm_getKeyRequest(AMediaDrm *d, const AMediaDrmScope *s, const uint8_t *i,
                                       size_t il, const char *m, AMediaDrmKeyType t,
                                       const AMediaDrmKeyValue *o, size_t on, const uint8_t **r,
                                       size_t *rl) {
    (void)d, (void)s, (void)i, (void)il, (void)m, (void)t, (void)o, (void)on;
    static const uint8_t req[] = "fake request";
    *r = req;
    *rl = sizeof req;
    return AMEDIA_OK;
}
media_status_t AMediaDrm_provideKeyResponse(AMediaDrm *d, const AMediaDrmScope *s, const uint8_t *r,
                                            size_t rl, AMediaDrmKeySetId *k) {
    (void)d, (void)s, (void)r, (void)rl, (void)k;
    return fake.reject_licence ? AMEDIA_DRM_LICENSE_EXPIRED : AMEDIA_OK;
}
media_status_t AMediaDrm_queryKeyStatus(AMediaDrm *d, const AMediaDrmSessionId *s,
                                        AMediaDrmKeyValue *kv, size_t *n) {
    (void)d, (void)s;
    if (*n < 1) return AMEDIA_DRM_SHORT_BUFFER;
    kv[0].mKey = "SecurityLevel";
    kv[0].mValue = "HW_SECURE_ALL";
    *n = 1;
    return AMEDIA_OK;
}

AMediaCrypto *AMediaCrypto_new(const AMediaUUID u, const void *i, size_t l) {
    (void)u, (void)i, (void)l;
    return calloc(1, sizeof(AMediaCrypto));
}
void AMediaCrypto_delete(AMediaCrypto *c) {
    free(c);
}
bool AMediaCrypto_requiresSecureDecoderComponent(const char *m) {
    (void)m;
    return true;
}

AMediaCodec *AMediaCodec_createCodecByName(const char *n) {
    if (fake.no_codec) return NULL;
    AMediaCodec *c = calloc(1, sizeof *c);
    if (c) c->audio = strstr(n, "audio") || strstr(n, "aac") || strstr(n, "raw");
    return c;
}
media_status_t AMediaCodec_delete(AMediaCodec *c) {
    free(c);
    return AMEDIA_OK;
}
media_status_t AMediaCodec_configure(AMediaCodec *c, const AMediaFormat *f, ANativeWindow *w,
                                     AMediaCrypto *k, uint32_t fl) {
    (void)c, (void)f, (void)w, (void)k, (void)fl;
    return AMEDIA_OK;
}
media_status_t AMediaCodec_start(AMediaCodec *c) {
    (void)c;
    return AMEDIA_OK;
}
media_status_t AMediaCodec_stop(AMediaCodec *c) {
    (void)c;
    return AMEDIA_OK;
}
media_status_t AMediaCodec_flush(AMediaCodec *c) {
    (void)c;
    return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec *c, int64_t t) {
    (void)c, (void)t;
    return fake.in_busy ? AMEDIACODEC_INFO_TRY_AGAIN_LATER : 0;
}
uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *c, size_t i, size_t *cap) {
    (void)c, (void)i;
    *cap = fake.in_cap ? fake.in_cap : sizeof inbuf;
    return inbuf;
}
media_status_t AMediaCodec_queueSecureInputBuffer(AMediaCodec *c, size_t i, off_t o,
                                                  AMediaCodecCryptoInfo *ci, uint64_t t,
                                                  uint32_t f) {
    (void)i, (void)o, (void)t, (void)f;
    fake.last_n = ci->n;
    fake.last_mode = ci->mode;
    fake.last_cl0 = ci->cl[0];
    fake.last_en0 = ci->en[0];
    fake.queued++;
    if (c->audio) atomic_store(&c->out_left, fake.audio_out);
    return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *c, AMediaCodecBufferInfo *inf, int64_t t) {
    (void)t;
    if (!c->audio || atomic_load(&c->out_left) <= 0) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    atomic_fetch_sub(&c->out_left, 1);
    inf->offset = 0;
    inf->size = fake.audio_out_size;
    inf->presentationTimeUs = 1000;
    inf->flags = 0;
    return 0;
}
uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *c, size_t i, size_t *cap) {
    (void)c, (void)i;
    memset(outbuf, 0x5a, sizeof outbuf);
    *cap = sizeof outbuf;
    return outbuf;
}
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *c, size_t i, bool r) {
    (void)c, (void)i, (void)r;
    return AMEDIA_OK;
}
AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *c) {
    (void)c;
    return calloc(1, sizeof(AMediaFormat));
}

AMediaCodecCryptoInfo *AMediaCodecCryptoInfo_new(int n, uint8_t key[16], uint8_t iv[16],
                                                 cryptoinfo_mode_t m, size_t *cl, size_t *en) {
    (void)key, (void)iv;
    AMediaCodecCryptoInfo *ci = calloc(1, sizeof *ci);
    if (!ci) return NULL;
    ci->n = n;
    ci->mode = m;
    ci->cl = malloc(sizeof(size_t) * (size_t)n);
    ci->en = malloc(sizeof(size_t) * (size_t)n);
    if (ci->cl) memcpy(ci->cl, cl, sizeof(size_t) * (size_t)n);
    if (ci->en) memcpy(ci->en, en, sizeof(size_t) * (size_t)n);
    return ci;
}
media_status_t AMediaCodecCryptoInfo_delete(AMediaCodecCryptoInfo *ci) {
    free(ci->cl);
    free(ci->en);
    free(ci);
    return AMEDIA_OK;
}
void AMediaCodecCryptoInfo_setPattern(AMediaCodecCryptoInfo *ci, cryptoinfo_pattern_t *p) {
    (void)ci, (void)p;
}

AMediaFormat *AMediaFormat_new(void) {
    return calloc(1, sizeof(AMediaFormat));
}
media_status_t AMediaFormat_delete(AMediaFormat *f) {
    free(f);
    return AMEDIA_OK;
}
void AMediaFormat_setString(AMediaFormat *f, const char *n, const char *v) {
    (void)f, (void)n, (void)v;
}
void AMediaFormat_setInt32(AMediaFormat *f, const char *n, int32_t v) {
    (void)f, (void)n, (void)v;
}
bool AMediaFormat_getInt32(AMediaFormat *f, const char *n, int32_t *o) {
    (void)f, (void)n;
    *o = 0;
    return true;
}
const char *AMediaFormat_toString(AMediaFormat *f) {
    (void)f;
    return "fake";
}

media_status_t AImageReader_newWithUsage(int32_t w, int32_t h, int32_t f, uint64_t u, int32_t m,
                                         AImageReader **r) {
    (void)w, (void)h, (void)f, (void)u, (void)m;
    *r = calloc(1, sizeof(AImageReader));
    return *r ? AMEDIA_OK : AMEDIA_ERROR_UNKNOWN;
}
void AImageReader_delete(AImageReader *r) {
    free(r);
}
media_status_t AImageReader_getWindow(AImageReader *r, ANativeWindow **w) {
    (void)r;
    static ANativeWindow win;
    *w = &win;
    return AMEDIA_OK;
}
media_status_t AImageReader_acquireNextImageAsync(AImageReader *r, AImage **i, int *fence) {
    (void)r;
    *i = NULL;
    *fence = -1;
    return AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;
}
void AImage_delete(AImage *i) {
    free(i);
}
media_status_t AImage_getHardwareBuffer(const AImage *i, AHardwareBuffer **b) {
    *b = (AHardwareBuffer *)(void *)fake.image_buffer;
    (void)i;
    return fake.image_buffer ? AMEDIA_OK : AMEDIA_ERROR_UNKNOWN;
}
