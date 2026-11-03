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

// Library CDM with the same name as Google's. It hands every session and every
// sample to the Android Widevine HAL through wvbridged. Video never comes back:
// it is decoded securely and shown on a hardware layer, and Chromium gets small
// black placeholder frames with the right timestamps to keep its clock running.
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#define CDM_IMPLEMENTATION
#include "media/cdm/api/content_decryption_module.h"
#include "proto.h"
using namespace cdm;
// Samples kept back from Chromium so its demand runs ahead of decoder latency.
#define HOLD 20
__attribute__((format(printf, 1, 2))) static void lg(const char *f, ...) {
    FILE *o = fopen(BR_LOG_HOST, "a");
    if (!o) return;
    va_list v;
    va_start(v, f);
    vfprintf(o, f, v);
    va_end(v);
    fputc('\n', o);
    fclose(o);
}
static int rd(int f, void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    while (n) {
        ssize_t r = read(f, b, n);
        if (r <= 0) return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}
static int wr(int f, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    while (n) {
        ssize_t r = write(f, b, n);
        if (r <= 0) return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}
struct Rep {
    int32_t st;
    uint32_t n;
    uint8_t *p;
    Rep() : st(-100), n(0), p(nullptr) {}
    ~Rep() { free(p); }
};
#define RE(p, m)                                                                                   \
    do {                                                                                           \
        const char *e_ = m;                                                                        \
        host->OnRejectPromise(p, kExceptionInvalidStateError, 1, e_, (uint32_t)strlen(e_));        \
    } while (0)
static void hex(const uint8_t *p, uint32_t n, char *o) {
    static const char h[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) {
        o[2 * i] = h[p[i] >> 4];
        o[2 * i + 1] = h[p[i] & 15];
    }
    o[2 * n] = 0;
}
static int unhex(const char *s, uint32_t n, uint8_t *o) {
    if (n & 1) return -1;
    for (uint32_t i = 0; i < n; i += 2) {
        int a = 0;
        for (int j = 0; j < 2; j++) {
            char c = s[i + j];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0) return -1;
            a = a * 16 + v;
        }
        o[i / 2] = (uint8_t)a;
    }
    return (int)(n / 2);
}
// Chromium's media clock, from its video renderer through CdmAdapter. Sent on to
// wvbridged, which holds frames until they are due.
extern "C" __attribute__((visibility("default"))) void
LuneOSBridgeClock(int64_t t0, int64_t m0, int64_t t1, int64_t m1, int32_t paused) {
    static int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return;
    struct {
        int64_t t0, m0, t1, m1;
        int32_t paused, pad;
    } c = {t0, m0, t1, m1, paused, 0};
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, BR_CLOCK_HOST, sizeof(a.sun_path) - 1);
    (void)sendto(fd, &c, sizeof c, MSG_NOSIGNAL, (struct sockaddr *)&a, sizeof a);
}
class Br final : public ContentDecryptionModule_10 {
    Host_10 *host;
    int fd;
    int pw, ph;
    int64_t pts[40];
    int npts;
    bool smp(const InputBuffer_2 &in, uint32_t video, Rep *r) {
        static uint8_t *b;
        static uint32_t cap;
        // wvbridged drops any message over 8 MiB; refuse it here, before the
        // sum can wrap on a 32-bit build.
        uint64_t need64 = sizeof(Smp) + (uint64_t)in.num_subsamples * 8 + in.data_size;
        if (need64 > 8 * 1024 * 1024) return false;
        uint32_t subs = in.num_subsamples * 8, need = (uint32_t)need64;
        if (need > cap) {
            free(b);
            cap = need + 65536;
            b = (uint8_t *)malloc(cap);
            if (!b) {
                cap = 0;
                return false;
            }
        }
        Smp h;
        memset(&h, 0, sizeof h);
        h.video = video;
        h.scheme = (uint32_t)in.encryption_scheme;
        h.ts = in.timestamp;
        h.size = in.data_size;
        h.nsub = in.num_subsamples;
        h.crypt = in.pattern.crypt_byte_block;
        h.skip = in.pattern.skip_byte_block;
        if (in.key_id && in.key_id_size == 16) memcpy(h.key, in.key_id, 16);
        if (in.iv && in.iv_size && in.iv_size <= 16) memcpy(h.iv, in.iv, in.iv_size);
        memcpy(b, &h, sizeof h);
        if (subs) memcpy(b + sizeof h, in.subsamples, subs);
        memcpy(b + sizeof h + subs, in.data, in.data_size);
        for (int i = 0; i < 12; i++) {
            free(r->p);
            r->p = nullptr;
            if (!call(OP_SAMPLE, b, need, nullptr, 0, r)) return false;
            if (r->st != ST_BUSY) return true;
        }
        return true;
    }
    Status pop(VideoFrame *f) {
        int m = 0;
        for (int i = 1; i < npts; i++)
            if (pts[i] < pts[m]) m = i;
        int64_t t = pts[m];
        pts[m] = pts[--npts];
        uint32_t ys = (uint32_t)(pw * ph), sz = ys * 3 / 2;
        Buffer *b = host->Allocate(sz);
        if (!b) return kDecodeError;
        uint8_t *p = b->Data();
        memset(p, 16, ys);
        memset(p + ys, 128, sz - ys);
        b->SetSize(sz);
        f->SetFormat(kI420);
        Size s;
        s.width = pw;
        s.height = ph;
        f->SetSize(s);
        f->SetFrameBuffer(b);
        f->SetPlaneOffset(kYPlane, 0);
        f->SetPlaneOffset(kUPlane, ys);
        f->SetPlaneOffset(kVPlane, ys + ys / 4);
        f->SetStride(kYPlane, (uint32_t)pw);
        f->SetStride(kUPlane, (uint32_t)pw / 2);
        f->SetStride(kVPlane, (uint32_t)pw / 2);
        f->SetTimestamp(t);
        return kSuccess;
    }
    bool call(uint8_t op, const void *a, uint32_t al, const void *b, uint32_t bl, Rep *r) {
        if (fd < 0) return false;
        uint32_t t = al + bl;
        if (wr(fd, &op, 1) || wr(fd, &t, 4) || (al && wr(fd, a, al)) || (bl && wr(fd, b, bl)) ||
            rd(fd, &r->st, 4) || rd(fd, &r->n, 4))
            return false;
        r->p = (uint8_t *)malloc(r->n ? r->n : 1);
        return r->p && (r->n == 0 || rd(fd, r->p, r->n) == 0);
    }
    bool scall(uint8_t op, const char *sid, uint32_t sl, const void *d, uint32_t dl, Rep *r) {
        uint8_t s[68];
        if (sl > 128) return false;
        int n = unhex(sid, sl, s + 4);
        if (n < 0) return false;
        uint32_t u = (uint32_t)n;
        memcpy(s, &u, 4);
        return call(op, s, 4 + u, d, dl, r);
    }

  public:
    explicit Br(Host_10 *h) : host(h), fd(-1), pw(64), ph(36), pts(), npts(0) {}
    void Initialize(bool, bool, bool hw) override {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a;
        memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        snprintf(a.sun_path, sizeof a.sun_path, "%s", BR_SOCK_HOST);
        int c = fd < 0 ? -1 : connect(fd, (sockaddr *)&a, sizeof a);
        lg("Initialize hw_secure=%d connect=%d", (int)hw, c);
        if (c) {
            if (fd >= 0) close(fd);
            fd = -1;
        }
        host->OnInitialized(c == 0);
    }
    void GetStatusForPolicy(uint32_t p, const Policy &) override {
        host->OnResolveKeyStatusPromise(p, kUsable);
    }
    void SetServerCertificate(uint32_t p, const uint8_t *d, uint32_t n) override {
        Rep r;
        bool ok = call(OP_CERT, d, n, nullptr, 0, &r);
        lg("SetServerCertificate %u bytes ok=%d st=%d", n, (int)ok, r.st);
        host->OnResolvePromise(p);
    }
    void CreateSessionAndGenerateRequest(uint32_t p, SessionType t, InitDataType it,
                                         const uint8_t *d, uint32_t n) override {
        lg("Create type=%d init=%d %u bytes", (int)t, (int)it, n);
        if (t != kTemporary || it != kCenc) {
            host->OnRejectPromise(p, kExceptionNotSupportedError, 0, "unsupported", 11);
            return;
        }
        Rep o;
        if (!call(OP_OPEN, nullptr, 0, nullptr, 0, &o) || o.st || o.n > 64) {
            RE(p, "open failed");
            return;
        }
        char hs[129];
        hex(o.p, o.n, hs);
        uint32_t hl = o.n * 2;
        Rep r;
        if (!scall(OP_REQUEST, hs, hl, d, n, &r) || r.st) {
            lg("request failed st=%d", r.st);
            RE(p, "request failed");
            return;
        }
        host->OnResolveNewSessionPromise(p, hs, hl);
        host->OnSessionMessage(hs, hl, kLicenseRequest, (const char *)r.p, r.n);
    }
    void LoadSession(uint32_t p, SessionType, const char *, uint32_t) override {
        host->OnRejectPromise(p, kExceptionNotSupportedError, 0, "unsupported", 11);
    }
    void UpdateSession(uint32_t p, const char *s, uint32_t sl, const uint8_t *d,
                       uint32_t n) override {
        Rep r;
        bool ok = scall(OP_UPDATE, s, sl, d, n, &r);
        lg("Update %u bytes ok=%d st=%d keys=%u", n, (int)ok, r.st, r.n / 16);
        if (!ok || r.st) {
            RE(p, "update failed");
            return;
        }
        uint32_t c = r.n / 16;
        if (c > 64) c = 64;
        KeyInformation k[64];
        for (uint32_t i = 0; i < c; i++) {
            k[i].key_id = r.p + 16 * i;
            k[i].key_id_size = 16;
            k[i].status = kUsable;
            k[i].system_code = 0;
        }
        host->OnSessionKeysChange(s, sl, c > 0, k, c);
        host->OnResolvePromise(p);
    }
    void CloseSession(uint32_t p, const char *s, uint32_t sl) override {
        Rep r;
        scall(OP_CLOSE, s, sl, nullptr, 0, &r);
        host->OnResolvePromise(p);
        host->OnSessionClosed(s, sl);
    }
    void RemoveSession(uint32_t p, const char *, uint32_t) override {
        host->OnRejectPromise(p, kExceptionNotSupportedError, 0, "unsupported", 11);
    }
    void TimerExpired(void *) override {}
    Status Decrypt(const InputBuffer_2 &in, DecryptedBlock *o) override {
        Rep r;
        if (!smp(in, 2, &r)) return kDecryptError;
        if (r.st == ST_NOKEY) return kNoKey;
        if (r.st || !r.n) {
            lg("decrypt st=%d n=%u", r.st, r.n);
            return kDecryptError;
        }
        Buffer *b = host->Allocate(r.n);
        if (!b) return kDecryptError;
        memcpy(b->Data(), r.p, r.n);
        b->SetSize(r.n);
        o->SetDecryptedBuffer(b);
        o->SetTimestamp(in.timestamp);
        return kSuccess;
    }
    Status InitializeAudioDecoder(const AudioDecoderConfig_2 &c) override {
        lg("InitAudio codec=%d ch=%d rate=%d extra=%u scheme=%d", (int)c.codec, c.channel_count,
           c.samples_per_second, c.extra_data_size, (int)c.encryption_scheme);
        if (c.codec != kCodecAac) return kInitializationError;
        Init i;
        i.codec = (uint32_t)c.codec;
        i.a = (uint32_t)c.channel_count;
        i.b = (uint32_t)c.samples_per_second;
        Rep r;
        return call(OP_AINIT, &i, sizeof i, nullptr, 0, &r) && !r.st ? kSuccess
                                                                     : kInitializationError;
    }
    Status InitializeVideoDecoder(const VideoDecoderConfig_2 &c) override {
        lg("InitVideo codec=%d %dx%d extra=%u scheme=%d", (int)c.codec, c.coded_size.width,
           c.coded_size.height, c.extra_data_size, (int)c.encryption_scheme);
        if (c.codec != kCodecH264 && c.codec != kCodecVp9) return kInitializationError;
        Init i;
        i.codec = (uint32_t)c.codec;
        i.a = (uint32_t)c.coded_size.width;
        i.b = (uint32_t)c.coded_size.height;
        pw = 64;
        ph = c.coded_size.width > 0 ? (64 * c.coded_size.height / c.coded_size.width) & ~1 : 36;
        if (ph < 2) ph = 2;
        Rep r;
        return call(OP_VINIT, &i, sizeof i, nullptr, 0, &r) && !r.st ? kSuccess
                                                                     : kInitializationError;
    }
    void DeinitializeDecoder(StreamType) override {}
    void ResetDecoder(StreamType t) override {
        uint32_t v = t == kStreamTypeVideo;
        if (v) npts = 0;
        Rep r;
        call(OP_RESET, &v, 4, nullptr, 0, &r);
        lg("Reset %u", v);
    }
    Status DecryptAndDecodeFrame(const InputBuffer_2 &in, VideoFrame *f) override {
        if (!in.data || !in.data_size) {
            if (!npts) return kNeedMoreData;
            return pop(f);
        }
        Rep r;
        if (!smp(in, 1, &r)) return kDecodeError;
        if (r.st == ST_NOKEY) {
            if (in.encryption_scheme != EncryptionScheme::kUnencrypted) return kNoKey;
            lg("clear sample before any session, dropped");
            return kNeedMoreData;
        }
        if (r.st == ST_BUSY) {
            lg("video input busy");
            return kDecodeError;
        }
        if (r.st) {
            lg("video sample st=%d", r.st);
            return kDecodeError;
        }
        if (npts < 40) pts[npts++] = in.timestamp;
        if (npts < HOLD) return kNeedMoreData;
        return pop(f);
    }
    Status DecryptAndDecodeSamples(const InputBuffer_2 &in, AudioFrames *a) override {
        if (!in.data || !in.data_size) return kNeedMoreData;
        Rep r;
        if (!smp(in, 0, &r)) return kDecodeError;
        if (r.st == ST_NOKEY)
            return in.encryption_scheme != EncryptionScheme::kUnencrypted ? kNoKey : kNeedMoreData;
        if (r.st) {
            lg("audio sample st=%d", r.st);
            return kDecodeError;
        }
        if (!r.n) return kNeedMoreData;
        Buffer *b = host->Allocate(r.n);
        if (!b) return kDecodeError;
        memcpy(b->Data(), r.p, r.n);
        b->SetSize(r.n);
        a->SetFrameBuffer(b);
        a->SetFormat(kAudioFormatS16);
        return kSuccess;
    }
    void OnPlatformChallengeResponse(const PlatformChallengeResponse &) override {}
    void OnQueryOutputProtectionStatus(QueryResult, uint32_t, uint32_t) override {}
    void OnStorageId(uint32_t, const uint8_t *, uint32_t) override {}
    void Destroy() override {
        if (fd >= 0) close(fd);
        lg("Destroy");
        delete this;
    }
};
extern "C" CDM_API void INITIALIZE_CDM_MODULE() {}
extern "C" CDM_API void DeinitializeCdmModule() {}
extern "C" CDM_API void *CreateCdmInstance(int v, const char *, uint32_t, GetCdmHostFunc g,
                                           void *u) {
    lg("CreateCdmInstance %d", v);
    if (v != 10) return nullptr;
    Host_10 *h = (Host_10 *)g(10, u);
    return h ? (void *)static_cast<ContentDecryptionModule_10 *>(new Br(h)) : nullptr;
}
extern "C" CDM_API const char *GetCdmVersion() {
    return "0.1-android-bridge";
}
