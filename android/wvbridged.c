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

// Runs inside the Android container. Gives one AMediaDrm per client connection
// and closes everything when the client goes away, because an open session
// keeps a liboemcrypto thread busy. Logs status only, never request or licence
// contents, IDs, certificates or keys.
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaCrypto.h>
#include <media/NdkMediaDrm.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "proto.h"
static const uint8_t WV[16] = {0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
                               0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};
#define MAXS 8
#define MAXK 64
#define OV "/data/local/tmp/wv/ov.sock"
typedef struct S {
    struct S *n;
    Smp h;
    uint8_t b[];
} Q;
typedef struct {
    int version, numFds, numInts;
    int data[];
} NH;
typedef struct {
    int magic, version;
    void *reserved[4];
    void *inc, *dec;
    int width, height, stride, format, usage_d;
    uintptr_t layerCount;
    void *res1;
    const NH *handle;
    uint64_t usage;
} ANWB;
typedef struct {
    uint32_t width, height, stride, format;
    uint64_t usage;
    int32_t numFds, numInts, hasFence, cropW, cropH;
    int32_t ints[96];
} VF;
typedef struct CS {
    int fd;
    AMediaDrm *d;
    AMediaDrmSessionId s[MAXS];
    uint8_t sb[MAXS][64];
    int used[MAXS];
    int privacy;
    int active;
    int opened;
    AMediaCrypto *cr;
    Init vi, ai;
    AMediaCodec *vc, *ac, *rc;
    Q *qh, *qt;
    int qn;
    AImageReader *ir;
    ANativeWindow *win;
    int sk;
    pthread_t vt;
    int vrun;
    volatile int gen;
    pthread_mutex_t m;
    long vin, vout, qerr, late, shown, ain, aout;
    int lastw, lasth;
} C;
static int reply(int f, int32_t st, const void *p, uint32_t n);
struct CS;
static int64_t due_of(int64_t pts);
static int64_t nowus(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}
static int anoff = -1;
static void send_frame(C *c, AImage *im, int fen) {
    AHardwareBuffer *ahb = NULL;
    if (AImage_getHardwareBuffer(im, &ahb) || !ahb) return;
    if (anoff < 0) {
        for (int o = 0; o <= 64; o += 8)
            if (*(uint32_t *)((char *)ahb + o) == 0x5f626672) {
                anoff = o;
                break;
            }
        if (anoff < 0) {
            puts("no buffer header");
            return;
        }
    }
    ANWB *an = (ANWB *)((char *)ahb + anoff);
    const NH *nh = an->handle;
    if (an->width != c->lastw || an->height != c->lasth) {
        printf("video buffer %dx%d\n", an->width, an->height);
        c->lastw = an->width;
        c->lasth = an->height;
    }
    if (c->sk < 0) return;
    VF f;
    memset(&f, 0, sizeof f);
    f.width = an->width;
    f.height = an->height;
    f.stride = an->stride;
    f.format = an->format;
    f.usage = an->usage;
    f.numFds = nh->numFds;
    f.numInts = nh->numInts;
    f.hasFence = fen >= 0;
    f.cropW = c->vi.a ? (int)c->vi.a : an->width;
    f.cropH = c->vi.b ? (int)c->vi.b : an->height;
    memcpy(f.ints, nh->data + nh->numFds, sizeof(int) * nh->numInts);
    int nf = nh->numFds + (fen >= 0);
    char cb[CMSG_SPACE(sizeof(int) * 32)];
    struct iovec io = {&f, sizeof f};
    struct msghdr m;
    memset(&m, 0, sizeof m);
    m.msg_iov = &io;
    m.msg_iovlen = 1;
    m.msg_control = cb;
    m.msg_controllen = CMSG_SPACE(sizeof(int) * nf);
    struct cmsghdr *ch = CMSG_FIRSTHDR(&m);
    ch->cmsg_level = SOL_SOCKET;
    ch->cmsg_type = SCM_RIGHTS;
    ch->cmsg_len = CMSG_LEN(sizeof(int) * nf);
    memcpy(CMSG_DATA(ch), nh->data, sizeof(int) * nh->numFds);
    if (fen >= 0) ((int *)CMSG_DATA(ch))[nh->numFds] = fen;
    if (sendmsg(c->sk, &m, MSG_NOSIGNAL) >= 0) c->shown++;
}
// Shows decoded frames on their own clock. Input arrives at Chromium's pace, so
// a pause just starves this loop and the clock is rebased on the next frame.
static void pump(struct CS *c);
static void qclear(struct CS *c);
// Extra video delay, read from a file so it can be tuned while playing.
static int64_t read_delay(void) {
    FILE *f = fopen("/data/local/tmp/wv/delay_us", "r");
    long long v = 0;
    if (f) {
        if (fscanf(f, "%lld", &v) != 1) v = 0;
        fclose(f);
    }
    return v;
}
static void *vloop(void *a) {
    C *c = a;
    AImage *hold[3] = {0};
    int64_t delay = read_delay(), lastrd = nowus();
    ssize_t o = -1;
    AMediaCodecBufferInfo inf;
    int g = c->gen;
    while (c->vrun) {
        pthread_mutex_lock(&c->m);
        pump(c);
        if (g != c->gen) {
            g = c->gen;
            o = -1;
        }
        if (o < 0) {
            o = AMediaCodec_dequeueOutputBuffer(c->vc, &inf, 0);
        }
        pthread_mutex_unlock(&c->m);
        if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *f = AMediaCodec_getOutputFormat(c->vc);
            int32_t w = 0, h = 0;
            AMediaFormat_getInt32(f, "width", &w);
            AMediaFormat_getInt32(f, "height", &h);
            printf("decoder output %dx%d\n", w, h);
            AMediaFormat_delete(f);
            o = -1;
            continue;
        }
        if (o < 0) {
            usleep(4000);
            continue;
        }
        int64_t n = nowus();
        if (n - lastrd > 1000000) {
            delay = read_delay();
            lastrd = n;
        }
        int64_t due = due_of(inf.presentationTimeUs);
        if (!due) {
            usleep(4000);
            continue;
        }
        due += delay;
        if (due - n > 6000) {
            usleep(due - n > 8000 ? 8000 : (useconds_t)(due - n));
            continue;
        }
        int drop = n - due > 45000;
        pthread_mutex_lock(&c->m);
        if (g == c->gen) AMediaCodec_releaseOutputBuffer(c->vc, o, !drop);
        pthread_mutex_unlock(&c->m);
        ssize_t was = o;
        o = -1;
        (void)was;
        if (g != c->gen) continue;
        if (drop) {
            c->late++;
            continue;
        }
        if (c->vout % 48 == 0) {
            struct timespec w;
            clock_gettime(CLOCK_REALTIME, &w);
            printf("shown %lld %lld\n", (long long)(w.tv_sec * 1000LL + w.tv_nsec / 1000000),
                   (long long)inf.presentationTimeUs / 1000);
        }
        c->vout++;
        AImage *im = NULL;
        int fen = -1;
        for (int t = 0; t < 40 && !im; t++) {
            if (AImageReader_acquireNextImageAsync(c->ir, &im, &fen) != AMEDIA_OK || !im) {
                im = NULL;
                usleep(1000);
            }
        }
        if (im) {
            send_frame(c, im, fen);
            if (fen >= 0) close(fen);
            if (hold[2]) AImage_delete(hold[2]);
            hold[2] = hold[1];
            hold[1] = hold[0];
            hold[0] = im;
        }
    }
    for (int i = 0; i < 3; i++)
        if (hold[i]) AImage_delete(hold[i]);
    return NULL;
}
// Latest clock from Chromium's video renderer: two points on the media to
// monotonic line, or paused.
typedef struct {
    int64_t t0, m0, t1, m1;
    int32_t paused, pad;
} Clk;
static Clk clk;
static volatile int clk_ok;
static pthread_mutex_t clk_m = PTHREAD_MUTEX_INITIALIZER;
static void *clockloop(void *a) {
    (void)a;
    int s = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un u;
    memset(&u, 0, sizeof u);
    u.sun_family = AF_UNIX;
    strcpy(u.sun_path, "/data/local/tmp/wv/clock.sock");
    unlink(u.sun_path);
    if (bind(s, (void *)&u, sizeof u)) {
        perror("clock bind");
        return NULL;
    }
    chmod(u.sun_path, 0666);
    for (;;) {
        Clk k;
        if (recv(s, &k, sizeof k, 0) != (ssize_t)sizeof k) continue;
        pthread_mutex_lock(&clk_m);
        if (k.paused)
            clk.paused = 1;
        else {
            clk = k;
            clk_ok = 1;
        }
        pthread_mutex_unlock(&clk_m);
    }
    return NULL;
}
// When pts is due on the monotonic clock. 0 means hold it: no clock yet or paused.
static int64_t due_of(int64_t pts) {
    Clk k;
    pthread_mutex_lock(&clk_m);
    k = clk;
    int ok = clk_ok;
    pthread_mutex_unlock(&clk_m);
    if (!ok || k.paused || k.m1 == k.m0) return 0;
    return k.t0 + (int64_t)((double)(pts - k.m0) * (double)(k.t1 - k.t0) / (double)(k.m1 - k.m0));
}
static int ensure_crypto(C *c) {
    if (c->cr) return 0;
    int u = c->active >= 0 ? c->active : c->opened;
    if (u < 0) return -1;
    c->cr = AMediaCrypto_new(WV, c->s[u].ptr, c->s[u].length);
    printf("crypto=%d requiresSecure=%d\n", c->cr != NULL,
           c->cr ? (int)AMediaCrypto_requiresSecureDecoderComponent("video/avc") : -1);
    return c->cr ? 0 : -2;
}
static int ensure_video(C *c) {
    if (c->vc) return 0;
    int r = ensure_crypto(c);
    if (r) return r;
    const char *nm = c->vi.codec == 3 ? "c2.qti.vp9.decoder.secure" : "c2.qti.avc.decoder.secure";
    const char *mt = c->vi.codec == 3 ? "video/x-vnd.on2.vp9" : "video/avc";
    media_status_t s = AImageReader_newWithUsage((int)c->vi.a, (int)c->vi.b, AIMAGE_FORMAT_PRIVATE,
                                                 AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT |
                                                     AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                                                     AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY,
                                                 6, &c->ir);
    printf("imagereader %ux%u: %d\n", c->vi.a, c->vi.b, (int)s);
    if (s) return -3;
    AImageReader_getWindow(c->ir, &c->win);
    c->sk = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, OV);
    if (connect(c->sk, (struct sockaddr *)&sa, sizeof sa)) {
        perror("compositor connect");
        close(c->sk);
        c->sk = -1;
    }
    c->vc = AMediaCodec_createCodecByName(nm);
    printf("codec %s: %d\n", nm, c->vc != NULL);
    if (!c->vc) return -4;
    AMediaFormat *f = AMediaFormat_new();
    AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, mt);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, (int)c->vi.a);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, (int)c->vi.b);
    AMediaFormat_setInt32(f, "max-width", 1920);
    AMediaFormat_setInt32(f, "max-height", 1088);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 4 * 1024 * 1024);
    s = AMediaCodec_configure(c->vc, f, c->win, c->cr, 0);
    AMediaFormat_delete(f);
    printf("configure: %d\n", (int)s);
    if (s) return -5;
    s = AMediaCodec_start(c->vc);
    printf("start: %d\n", (int)s);
    if (s) return -6;
    c->vrun = 1;
    pthread_create(&c->vt, NULL, vloop, c);
    return 0;
}
static int ensure_audio(C *c) {
    if (c->ac) return 0;
    int r = ensure_crypto(c);
    if (r) return r;
    c->ac = AMediaCodec_createCodecByName("c2.android.aac.decoder");
    printf("aac codec: %d\n", c->ac != NULL);
    if (!c->ac) return -4;
    AMediaFormat *f = AMediaFormat_new();
    AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, (int)c->ai.b);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, (int)c->ai.a);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_IS_ADTS, 1);
    media_status_t s = AMediaCodec_configure(c->ac, f, NULL, c->cr, 0);
    AMediaFormat_delete(f);
    printf("audio configure: %d\n", (int)s);
    if (s) return -5;
    s = AMediaCodec_start(c->ac);
    printf("audio start: %d\n", (int)s);
    return s ? -6 : 0;
}
// Decrypt only: the raw decoder passes its input straight through, so a sample
// queued with crypto comes back as the clear compressed audio Chromium wants.
static int ensure_raw(C *c) {
    if (c->rc) return 0;
    int r = ensure_crypto(c);
    if (r) return r;
    c->rc = AMediaCodec_createCodecByName("c2.android.raw.decoder");
    printf("raw codec: %d\n", c->rc != NULL);
    if (!c->rc) return -4;
    AMediaFormat *f = AMediaFormat_new();
    AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/raw");
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, 48000);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 256 * 1024);
    media_status_t s = AMediaCodec_configure(c->rc, f, NULL, c->cr, 0);
    AMediaFormat_delete(f);
    printf("raw configure: %d\n", (int)s);
    if (s) return -5;
    s = AMediaCodec_start(c->rc);
    printf("raw start: %d\n", (int)s);
    return s ? -6 : 0;
}
static void stop_media(C *c) {
    if (c->rc) {
        AMediaCodec_stop(c->rc);
        AMediaCodec_delete(c->rc);
        c->rc = NULL;
    }
    if (c->vrun) {
        c->vrun = 0;
        pthread_join(c->vt, NULL);
    }
    qclear(c);
    if (c->vc) {
        AMediaCodec_stop(c->vc);
        AMediaCodec_delete(c->vc);
        c->vc = NULL;
    }
    if (c->ac) {
        AMediaCodec_stop(c->ac);
        AMediaCodec_delete(c->ac);
        c->ac = NULL;
    }
    if (c->sk >= 0) {
        close(c->sk);
        c->sk = -1;
    }
    if (c->ir) {
        AImageReader_delete(c->ir);
        c->ir = NULL;
    }
    if (c->cr) {
        AMediaCrypto_delete(c->cr);
        c->cr = NULL;
    }
    if (c->vin || c->ain)
        printf("video in=%ld out=%ld shown=%ld dropped=%ld queueErrors=%ld, audio in=%ld out=%ld\n",
               c->vin, c->vout, c->shown, c->late, c->qerr, c->ain, c->aout);
    c->vin = c->ain = 0;
}
static int queue(C *c, AMediaCodec *m, const Smp *h, const uint32_t *sub, const uint8_t *d,
                 int64_t wait) {
    ssize_t i = AMediaCodec_dequeueInputBuffer(m, wait);
    if (i < 0) return ST_BUSY;
    size_t cap = 0;
    uint8_t *b = AMediaCodec_getInputBuffer(m, i, &cap);
    if (!b || cap < h->size) {
        printf("input buffer %zu too small for %u\n", cap, h->size);
        return -7;
    }
    memcpy(b, d, h->size);
    uint32_t n = h->nsub ? h->nsub : 1;
    size_t *cl = calloc(n, sizeof(size_t)), *en = calloc(n, sizeof(size_t));
    for (uint32_t k = 0; k < h->nsub; k++) {
        cl[k] = sub[2 * k];
        en[k] = sub[2 * k + 1];
    }
    cryptoinfo_mode_t mode = AMEDIACODECRYPTOINFO_MODE_CLEAR;
    if (h->scheme == 1)
        mode = AMEDIACODECRYPTOINFO_MODE_AES_CTR;
    else if (h->scheme == 2)
        mode = AMEDIACODECRYPTOINFO_MODE_AES_CBC;
    if (!h->nsub) {
        if (h->scheme)
            en[0] = h->size;
        else
            cl[0] = h->size;
    }
    AMediaCodecCryptoInfo *ci =
        AMediaCodecCryptoInfo_new((int)n, (uint8_t *)h->key, (uint8_t *)h->iv, mode, cl, en);
    if (h->scheme == 2) {
        cryptoinfo_pattern_t p = {(int32_t)h->crypt, (int32_t)h->skip};
        AMediaCodecCryptoInfo_setPattern(ci, &p);
    }
    media_status_t s = AMediaCodec_queueSecureInputBuffer(m, i, 0, ci, h->ts, 0);
    AMediaCodecCryptoInfo_delete(ci);
    free(cl);
    free(en);
    if (s) {
        if (c->qerr++ < 5)
            printf("queueSecure: %d (video=%u scheme=%u)\n", (int)s, h->video, h->scheme);
        return -8;
    }
    return 0;
}
// Queues waiting samples into the decoder. Called with c->m held.
static void pump(struct CS *c) {
    while (c->qh) {
        Q *q = c->qh;
        int r = queue(c, c->vc, &q->h, (const uint32_t *)q->b, q->b + q->h.nsub * 8, 0);
        if (r == ST_BUSY) return;
        c->qh = q->n;
        if (!c->qh) c->qt = NULL;
        c->qn--;
        free(q);
        if (r) return;
        c->vin++;
    }
}
static void qclear(struct CS *c) {
    while (c->qh) {
        Q *q = c->qh;
        c->qh = q->n;
        free(q);
    }
    c->qt = NULL;
    c->qn = 0;
}
#define PCMCAP (1024 * 1024)
static int sample(C *c, const uint8_t *b, uint32_t n) {
    if (n < sizeof(Smp)) return -9;
    Smp h;
    memcpy(&h, b, sizeof h);
    uint64_t need = sizeof h + (uint64_t)h.nsub * 8 + h.size;
    if (need != n) return -9;
    const uint32_t *sub = (const uint32_t *)(b + sizeof h);
    const uint8_t *d = b + sizeof h + h.nsub * 8;
    if (h.scheme && c->active < 0) return reply(c->fd, ST_NOKEY, NULL, 0);
    if (h.video == 1) {
        int r = ensure_video(c);
        if (r == -1) return reply(c->fd, ST_NOKEY, NULL, 0);
        if (r) return reply(c->fd, r, NULL, 0);
        size_t qs = sizeof(Q) + h.nsub * 8 + h.size;
        Q *q = malloc(qs);
        if (!q) return reply(c->fd, -7, NULL, 0);
        q->n = NULL;
        q->h = h;
        memcpy(q->b, sub, h.nsub * 8);
        memcpy(q->b + h.nsub * 8, d, h.size);
        pthread_mutex_lock(&c->m);
        if (c->qt)
            c->qt->n = q;
        else
            c->qh = q;
        c->qt = q;
        c->qn++;
        pthread_mutex_unlock(&c->m);
        return reply(c->fd, 0, NULL, 0);
    }
    if (h.video == 2) {
        int r = ensure_raw(c);
        if (r == -1) return reply(c->fd, ST_NOKEY, NULL, 0);
        if (r) return reply(c->fd, r, NULL, 0);
        r = queue(c, c->rc, &h, sub, d, 30000);
        if (r) return reply(c->fd, r, NULL, 0);
        c->ain++;
        for (int t = 0; t < 40; t++) {
            AMediaCodecBufferInfo inf;
            ssize_t o = AMediaCodec_dequeueOutputBuffer(c->rc, &inf, 5000);
            if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
                o == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
                continue;
            if (o < 0) continue;
            size_t cap;
            uint8_t *p = AMediaCodec_getOutputBuffer(c->rc, o, &cap);
            int rr = reply(c->fd, 0, p ? p + inf.offset : NULL, p ? (uint32_t)inf.size : 0);
            AMediaCodec_releaseOutputBuffer(c->rc, o, false);
            c->aout++;
            if (c->aout == 1) printf("first decrypted sample: in %u out %d\n", h.size, inf.size);
            return rr;
        }
        return reply(c->fd, -10, NULL, 0);
    }
    int r = ensure_audio(c);
    if (r == -1) return reply(c->fd, ST_NOKEY, NULL, 0);
    if (r) return reply(c->fd, r, NULL, 0);
    r = queue(c, c->ac, &h, sub, d, 30000);
    if (r) return reply(c->fd, r, NULL, 0);
    c->ain++;
    static __thread uint8_t *pc;
    if (!pc) pc = malloc(PCMCAP);
    uint32_t off = 0;
    for (;;) {
        AMediaCodecBufferInfo inf;
        ssize_t o = AMediaCodec_dequeueOutputBuffer(c->ac, &inf, off ? 0 : 4000);
        if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *f = AMediaCodec_getOutputFormat(c->ac);
            printf("audio output: %s\n", AMediaFormat_toString(f));
            AMediaFormat_delete(f);
            continue;
        }
        if (o == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
        if (o < 0) break;
        size_t cap;
        uint8_t *p = AMediaCodec_getOutputBuffer(c->ac, o, &cap);
        if (p && inf.size > 0 && off + 16 + (uint32_t)inf.size <= PCMCAP) {
            int64_t ts = inf.presentationTimeUs, sz = inf.size;
            memcpy(pc + off, &ts, 8);
            memcpy(pc + off + 8, &sz, 8);
            memcpy(pc + off + 16, p + inf.offset, inf.size);
            off += 16 + (uint32_t)inf.size;
            c->aout++;
        }
        AMediaCodec_releaseOutputBuffer(c->ac, o, false);
    }
    return reply(c->fd, 0, pc, off);
}
static int rd(int f, void *p, size_t n) {
    uint8_t *b = p;
    while (n) {
        ssize_t r = read(f, b, n);
        if (r <= 0) return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}
static int wr(int f, const void *p, size_t n) {
    const uint8_t *b = p;
    while (n) {
        ssize_t r = write(f, b, n);
        if (r <= 0) return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}
static int reply(int f, int32_t st, const void *p, uint32_t n) {
    return wr(f, &st, 4) || wr(f, &n, 4) || (n && wr(f, p, n)) ? -1 : 0;
}
static uint64_t vi(const uint8_t **p, const uint8_t *e, int *ok) {
    uint64_t v = 0;
    int sh = 0;
    while (*p < e && sh < 64) {
        uint8_t c = *(*p)++;
        v |= (uint64_t)(c & 127) << sh;
        if (!(c & 128)) return v;
        sh += 7;
    }
    *ok = 0;
    return 0;
}
// Walks one protobuf message and calls back for each field.
typedef void (*fld)(void *u, uint32_t no, uint64_t v, const uint8_t *b, size_t l);
static int walk(const uint8_t *p, size_t n, fld f, void *u) {
    const uint8_t *e = p + n;
    int ok = 1;
    while (p < e) {
        uint64_t t = vi(&p, e, &ok);
        if (!ok) return -1;
        uint32_t no = (uint32_t)(t >> 3);
        switch (t & 7) {
        case 0: {
            uint64_t v = vi(&p, e, &ok);
            if (!ok) return -1;
            f(u, no, v, NULL, 0);
            break;
        }
        case 2: {
            uint64_t l = vi(&p, e, &ok);
            if (!ok || l > (uint64_t)(e - p)) return -1;
            f(u, no, 0, p, (size_t)l);
            p += l;
            break;
        }
        case 1:
            if (e - p < 8) return -1;
            p += 8;
            break;
        case 5:
            if (e - p < 4) return -1;
            p += 4;
            break;
        default:
            return -1;
        }
    }
    return 0;
}
typedef struct {
    uint8_t ids[MAXK][16];
    int n;
    int lvl[8];
    int content;
    int other;
    const uint8_t *msg;
    size_t ml;
    const uint8_t *kid;
    size_t kl;
    uint64_t ty, le;
} K;
static void kc(void *u, uint32_t no, uint64_t v, const uint8_t *b, size_t l) {
    K *k = u;
    if (no == 1) {
        k->kid = b;
        k->kl = l;
    } else if (no == 4)
        k->ty = v;
    else if (no == 5)
        k->le = v;
}
static void lic(void *u, uint32_t no, uint64_t v, const uint8_t *b, size_t l) {
    (void)v;
    K *k = u;
    if (no != 3 || !b) return;
    k->kid = NULL;
    k->kl = 0;
    k->ty = 0;
    k->le = 0;
    if (walk(b, l, kc, k)) return;
    if (k->ty == 2) {
        k->content++;
        if (k->le < 8) k->lvl[k->le]++;
        if (k->kl == 16 && k->n < MAXK) {
            memcpy(k->ids[k->n], k->kid, 16);
            k->n++;
        }
    } else
        k->other++;
}
static void sgn(void *u, uint32_t no, uint64_t v, const uint8_t *b, size_t l) {
    (void)v;
    K *k = u;
    if (no == 2 && b) {
        k->msg = b;
        k->ml = l;
    }
}
static int slot(C *c, const uint8_t *p, uint32_t n) {
    for (int i = 0; i < MAXS; i++)
        if (c->used[i] && c->s[i].length == n && !memcmp(c->s[i].ptr, p, n)) return i;
    return -1;
}
static void *client(void *a) {
    C *c = a;
    int f = c->fd;
    c->active = -1;
    c->opened = -1;
    c->sk = -1;
    pthread_mutex_init(&c->m, NULL);
    printf("client connected\n");
    c->d = AMediaDrm_createByUUID(WV);
    if (!c->d) printf("createByUUID failed\n");
    for (;;) {
        uint8_t op;
        uint32_t n;
        if (rd(f, &op, 1) || rd(f, &n, 4)) break;
        if (n > 8 * 1024 * 1024) break;
        uint8_t *b = malloc(n ? n : 1);
        if (!b || rd(f, b, n)) {
            free(b);
            break;
        }
        media_status_t s = AMEDIA_ERROR_UNKNOWN;
        if (!c->d) {
            reply(f, -1, NULL, 0);
            free(b);
            continue;
        }
        if (op == OP_CERT) {
            s = AMediaDrm_setPropertyByteArray(c->d, "serviceCertificate", b, n);
            printf("serviceCertificate: %u bytes, status %d\n", n, (int)s);
            media_status_t q = AMediaDrm_setPropertyString(c->d, "privacyMode", "enable");
            printf("privacyMode: status %d\n", (int)q);
            c->privacy = 1;
            reply(f, s, NULL, 0);
        } else if (op == OP_OPEN) {
            int i;
            for (i = 0; i < MAXS && c->used[i]; i++) {
            }
            if (i == MAXS) {
                reply(f, -2, NULL, 0);
                free(b);
                continue;
            }
            s = AMediaDrm_openSession(c->d, &c->s[i]);
            printf("openSession: status %d\n", (int)s);
            if (s == AMEDIA_OK) {
                memcpy(c->sb[i], c->s[i].ptr, c->s[i].length > 64 ? 64 : c->s[i].length);
                c->used[i] = 1;
                c->opened = i;
                reply(f, 0, c->s[i].ptr, (uint32_t)c->s[i].length);
            } else
                reply(f, s, NULL, 0);
        } else if (op == OP_REQUEST || op == OP_UPDATE || op == OP_CLOSE) {
            uint32_t sl = 0;
            if (n >= 4) memcpy(&sl, b, 4);
            int i = (n >= 4 && sl <= n - 4) ? slot(c, b + 4, sl) : -1;
            if (i < 0) {
                reply(f, -3, NULL, 0);
                free(b);
                continue;
            }
            const uint8_t *d = b + 4 + sl;
            size_t dl = n - 4 - sl;
            if (op == OP_REQUEST) {
                const uint8_t *r = NULL;
                size_t rl = 0;
                s = AMediaDrm_getKeyRequest(c->d, &c->s[i], d, dl, "video/mp4", KEY_TYPE_STREAMING,
                                            NULL, 0, &r, &rl);
                printf("getKeyRequest: %zu init bytes, status %d, %zu bytes\n", dl, (int)s, rl);
                reply(f, s, s == AMEDIA_OK ? r : NULL, s == AMEDIA_OK ? (uint32_t)rl : 0);
            } else if (op == OP_UPDATE) {
                AMediaDrmKeySetId ks;
                memset(&ks, 0, sizeof ks);
                s = AMediaDrm_provideKeyResponse(c->d, &c->s[i], d, dl, &ks);
                printf("provideKeyResponse: %zu bytes, status %d\n", dl, (int)s);
                static K k;
                memset(&k, 0, sizeof k);
                if (s == AMEDIA_OK) {
                    if (!walk(d, dl, sgn, &k) && k.msg) walk(k.msg, k.ml, lic, &k);
                    printf("licence: %d content keys, %d other, level sw_crypto=%d sw_decode=%d "
                           "hw_crypto=%d hw_decode=%d hw_all=%d\n",
                           k.content, k.other, k.lvl[1], k.lvl[2], k.lvl[3], k.lvl[4], k.lvl[5]);
                    AMediaDrmKeyValue kv[32];
                    size_t ct = 32;
                    media_status_t q = AMediaDrm_queryKeyStatus(c->d, &c->s[i], kv, &ct);
                    static const char *w[] = {"LicenseType",
                                              "PlayAllowed",
                                              "PersistAllowed",
                                              "RenewAllowed",
                                              "LicenseDurationRemaining",
                                              "PlaybackDurationRemaining",
                                              "SecurityLevel"};
                    if (q == AMEDIA_OK)
                        for (size_t x = 0; x < ct; x++)
                            for (size_t y = 0; y < 7; y++)
                                if (!strcmp(kv[x].mKey, w[y]))
                                    printf("  %s=%s\n", kv[x].mKey, kv[x].mValue);
                }
                if (s == AMEDIA_OK) c->active = i;
                reply(f, s, k.ids, (uint32_t)k.n * 16);
            } else {
                if (c->active == i || c->opened == i) {
                    stop_media(c);
                    if (c->active == i) c->active = -1;
                    if (c->opened == i) c->opened = -1;
                }
                s = AMediaDrm_closeSession(c->d, &c->s[i]);
                c->used[i] = 0;
                printf("closeSession: status %d\n", (int)s);
                reply(f, s, NULL, 0);
            }
        } else if (op == OP_VINIT && n >= sizeof(Init)) {
            memcpy(&c->vi, b, sizeof(Init));
            printf("video config codec=%u %ux%u\n", c->vi.codec, c->vi.a, c->vi.b);
            reply(f, 0, NULL, 0);
        } else if (op == OP_AINIT && n >= sizeof(Init)) {
            memcpy(&c->ai, b, sizeof(Init));
            printf("audio config ch=%u rate=%u\n", c->ai.a, c->ai.b);
            reply(f, 0, NULL, 0);
        } else if (op == OP_SAMPLE) {
            int r = sample(c, b, n);
            if (r == -9) reply(f, -9, NULL, 0);
        } else if (op == OP_RESET && n >= 4) {
            uint32_t v;
            memcpy(&v, b, 4);
            if (v && c->vc) {
                pthread_mutex_lock(&c->m);
                c->gen++;
                qclear(c);
                AMediaCodec_flush(c->vc);
                pthread_mutex_lock(&clk_m);
                clk_ok = 0;
                pthread_mutex_unlock(&clk_m);
                pthread_mutex_unlock(&c->m);
            }
            if (!v && c->ac) AMediaCodec_flush(c->ac);
            if (!v && c->rc) AMediaCodec_flush(c->rc);
            reply(f, 0, NULL, 0);
        } else
            reply(f, -4, NULL, 0);
        free(b);
    }
    stop_media(c);
    if (c->d) {
        for (int i = 0; i < MAXS; i++)
            if (c->used[i]) {
                AMediaDrm_closeSession(c->d, &c->s[i]);
            }
        AMediaDrm_release(c->d);
    }
    close(f);
    printf("client gone\n");
    free(c);
    return NULL;
}
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    int l = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, BR_SOCK_DROID);
    unlink(a.sun_path);
    if (bind(l, (void *)&a, sizeof a) || listen(l, 4)) {
        perror("bind");
        return 1;
    }
    chmod(a.sun_path, 0666);
    pthread_t ct;
    pthread_create(&ct, NULL, clockloop, NULL);
    puts("listening");
    for (;;) {
        int f = accept(l, NULL, NULL);
        if (f < 0) continue;
        C *c = calloc(1, sizeof *c);
        c->fd = f;
        pthread_t t;
        pthread_create(&t, NULL, client, c);
        pthread_detach(t);
    }
}
