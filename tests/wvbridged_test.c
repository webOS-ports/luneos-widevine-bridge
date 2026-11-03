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

// Host tests for wvbridged: the licence parser, the request protocol (sent
// over a socketpair to a real client() thread) and the compositor message.
// The NDK is faked (fake_ndk.c). All data here is made up; nothing comes from
// a device or a licence server.
#define main wvbridged_main
#include "../android/wvbridged.c"
#undef main
#include <errno.h>
#include <fcntl.h>
#include "fake_ndk.h"

static atomic_int failed; // CHECK runs on client threads too
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);                  \
            failed++;                                                                              \
        }                                                                                          \
    } while (0)

// ---- a minimal protobuf writer, for building licences ----
typedef struct {
    uint8_t b[8192];
    size_t n;
} PB;
static void pb_varint(PB *p, uint64_t v) {
    do {
        uint8_t c = v & 127;
        v >>= 7;
        p->b[p->n++] = (uint8_t)(c | (v ? 128 : 0));
    } while (v);
}
static void pb_uint(PB *p, uint32_t no, uint64_t v) {
    pb_varint(p, (uint64_t)no << 3);
    pb_varint(p, v);
}
static void pb_bytes(PB *p, uint32_t no, const void *d, size_t l) {
    pb_varint(p, (uint64_t)no << 3 | 2);
    pb_varint(p, l);
    memcpy(p->b + p->n, d, l);
    p->n += l;
}
static void key(PB *lic, uint8_t id, uint64_t type, uint64_t level) {
    PB k = {.n = 0};
    uint8_t kid[16];
    memset(kid, id, sizeof kid);
    pb_bytes(&k, 1, kid, sizeof kid);
    pb_uint(&k, 4, type);
    pb_uint(&k, 5, level);
    pb_bytes(lic, 3, k.b, k.n);
}
// SignedMessage{2: License{3: KeyContainer...}}
static void licence(PB *out, int content, int other) {
    PB lic = {.n = 0};
    for (int i = 0; i < content; i++) key(&lic, (uint8_t)(i + 1), 2, 5);
    for (int i = 0; i < other; i++) key(&lic, 0xee, 1, 0);
    out->n = 0;
    pb_uint(out, 1, 2);
    pb_bytes(out, 2, lic.b, lic.n);
}
static int parse(const uint8_t *p, size_t n, K *k) {
    memset(k, 0, sizeof *k);
    int r = walk(p, n, sgn, k);
    if (!r && k->msg) r = walk(k->msg, k->ml, lic, k);
    return r;
}

static void test_licence_parse(void) {
    PB m;
    K k;
    licence(&m, 3, 2);
    CHECK(parse(m.b, m.n, &k) == 0);
    CHECK(k.content == 3);
    CHECK(k.other == 2);
    CHECK(k.n == 3);
    CHECK(k.lvl[5] == 3);
    CHECK(k.ids[0][0] == 1 && k.ids[2][15] == 3);
}

static void test_licence_key_cap(void) {
    PB m;
    K k;
    licence(&m, MAXK + 5, 0);
    CHECK(parse(m.b, m.n, &k) == 0);
    CHECK(k.content == MAXK + 5);
    CHECK(k.n == MAXK);
}

static void test_licence_bad_input(void) {
    K k;
    // Every truncation of a good licence: an error or a clean parse, never a
    // read past the end (ASan/valgrind watch that).
    PB m;
    licence(&m, 2, 1);
    for (size_t l = 0; l < m.n; l++) {
        uint8_t *cut = malloc(l ? l : 1);
        memcpy(cut, m.b, l);
        (void)parse(cut, l, &k);
        free(cut);
    }
    // A varint longer than 64 bits.
    uint8_t over[12];
    memset(over, 0xff, sizeof over);
    CHECK(parse(over, sizeof over, &k) == -1);
    // A length that runs past the end, and one near 2^64.
    PB p = {.n = 0};
    pb_varint(&p, 2 << 3 | 2);
    pb_varint(&p, 1000);
    CHECK(parse(p.b, p.n, &k) == -1);
    p.n = 0;
    pb_varint(&p, 2 << 3 | 2);
    pb_varint(&p, UINT64_MAX - 1);
    CHECK(parse(p.b, p.n, &k) == -1);
    // Wire types 3, 4, 6, 7 are refused; fixed64 and fixed32 need their bytes.
    uint8_t wt3[] = {0x0b};
    CHECK(parse(wt3, sizeof wt3, &k) == -1);
    uint8_t f64[] = {0x09, 1, 2, 3};
    CHECK(parse(f64, sizeof f64, &k) == -1);
    uint8_t f32[] = {0x0d, 1, 2};
    CHECK(parse(f32, sizeof f32, &k) == -1);
    // A key level past the lvl[] table is counted, not stored.
    PB lic = {.n = 0};
    key(&lic, 9, 2, 200);
    PB s = {.n = 0};
    pb_bytes(&s, 2, lic.b, lic.n);
    CHECK(parse(s.b, s.n, &k) == 0);
    CHECK(k.content == 1);
}

// Random byte flips, inserts and cuts on good licences.
static void test_licence_fuzz(void) {
    const char *e = getenv("WV_FUZZ_ITERS");
    long iters = e ? atol(e) : 20000;
    uint64_t x = 0x9e3779b97f4a7c15ULL;
    PB m;
    for (long i = 0; i < iters; i++) {
        licence(&m, (int)(i % 5), (int)(i % 3));
        x ^= x << 13, x ^= x >> 7, x ^= x << 17;
        int muts = 1 + (int)(x % 6);
        for (int j = 0; j < muts && m.n; j++) {
            x ^= x << 13, x ^= x >> 7, x ^= x << 17;
            size_t at = (size_t)(x % m.n);
            switch ((x >> 32) % 3) {
            case 0:
                m.b[at] = (uint8_t)(x >> 40);
                break;
            case 1:
                m.n = at;
                break;
            default:
                if (m.n < sizeof m.b) {
                    memmove(m.b + at + 1, m.b + at, m.n - at);
                    m.b[at] = (uint8_t)(x >> 48);
                    m.n++;
                }
            }
        }
        uint8_t *exact = malloc(m.n ? m.n : 1); // exact size, so ASan sees any overread
        memcpy(exact, m.b, m.n);
        K k;
        (void)parse(exact, m.n, &k);
        CHECK(k.n <= MAXK);
        free(exact);
    }
}

// ---- the request protocol, against a real client() thread ----
typedef struct {
    int fd;
    pthread_t t;
} Conn;
static Conn conn(void) {
    int sv[2];
    Conn k = {-1, 0};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) return k;
    C *c = calloc(1, sizeof *c);
    c->fd = sv[1];
    k.fd = sv[0];
    // A daemon that stops answering fails the test instead of hanging it.
    struct timeval tv = {5, 0};
    setsockopt(k.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    pthread_create(&k.t, NULL, client, c);
    return k;
}
static void hangup(Conn *k) {
    close(k->fd);
    pthread_join(k->t, NULL);
}
static int send_req(int fd, uint8_t op, const void *p, uint32_t n) {
    return wr(fd, &op, 1) || wr(fd, &n, 4) || (n && wr(fd, p, n));
}
// Returns the status, or INT32_MIN when the connection went away.
static int32_t call(int fd, uint8_t op, const void *p, uint32_t n, uint8_t *out, uint32_t *ol) {
    int32_t st;
    uint32_t l;
    if (send_req(fd, op, p, n) || rd(fd, &st, 4) || rd(fd, &l, 4)) return INT32_MIN;
    uint8_t *b = malloc(l ? l : 1);
    if (rd(fd, b, l)) {
        free(b);
        return INT32_MIN;
    }
    if (out && ol) {
        uint32_t c = l < *ol ? l : *ol;
        memcpy(out, b, c);
        *ol = l;
    }
    free(b);
    return st;
}
static int closed(int fd) {
    uint8_t x;
    return read(fd, &x, 1) == 0; // a timeout (-1) is not closed
}
// Opens a session; sid gets [u32 len][id].
static uint32_t open_session(int fd, uint8_t *sid) {
    uint32_t l = 64;
    if (call(fd, OP_OPEN, NULL, 0, sid + 4, &l)) return 0;
    memcpy(sid, &l, 4);
    return 4 + l;
}

static void test_protocol_malformed(void) {
    Conn k = conn();
    CHECK(call(k.fd, 99, NULL, 0, NULL, NULL) == -4);
    CHECK(call(k.fd, OP_REQUEST, "ab", 2, NULL, NULL) == -3);
    uint8_t bad[8] = {200, 0, 0, 0}; // session id length 200 in an 8-byte message
    CHECK(call(k.fd, OP_REQUEST, bad, sizeof bad, NULL, NULL) == -3);
    uint8_t unknown[12] = {8, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8}; // no such session
    CHECK(call(k.fd, OP_UPDATE, unknown, sizeof unknown, NULL, NULL) == -3);
    CHECK(call(k.fd, OP_CLOSE, unknown, sizeof unknown, NULL, NULL) == -3);
    CHECK(call(k.fd, OP_VINIT, "x", 1, NULL, NULL) == -4);
    CHECK(call(k.fd, OP_AINIT, "x", 1, NULL, NULL) == -4);
    CHECK(call(k.fd, OP_RESET, "x", 1, NULL, NULL) == -4);
    CHECK(call(k.fd, OP_SAMPLE, "short", 5, NULL, NULL) == -9);
    // A sample header whose sizes do not add up to the message.
    uint8_t s[sizeof(Smp) + 4];
    memset(s, 0, sizeof s);
    Smp h;
    memset(&h, 0, sizeof h);
    h.size = 100;
    memcpy(s, &h, sizeof h);
    CHECK(call(k.fd, OP_SAMPLE, s, sizeof s, NULL, NULL) == -9);
    h.size = 4;
    h.nsub = 0x20000000; // nsub * 8 wraps in 32 bits
    memcpy(s, &h, sizeof h);
    CHECK(call(k.fd, OP_SAMPLE, s, sizeof s, NULL, NULL) == -9);
    // Encrypted with no key yet.
    h.nsub = 0;
    h.scheme = 1;
    memcpy(s, &h, sizeof h);
    CHECK(call(k.fd, OP_SAMPLE, s, sizeof s, NULL, NULL) == ST_NOKEY);
    // Still answering after all that.
    uint8_t sid[68];
    CHECK(open_session(k.fd, sid) == 12);
    // An id length matching that session's 8 bytes, with only 2 sent: refused
    // without comparing past the message.
    uint8_t cut[6] = {8, 0, 0, 0};
    memcpy(cut + 4, sid + 4, 2);
    CHECK(call(k.fd, OP_UPDATE, cut, sizeof cut, NULL, NULL) == -3);
    hangup(&k);
}

static void test_protocol_limits(void) {
    // More than 8 MiB announced: the connection is dropped, nothing allocated.
    Conn k = conn();
    uint8_t op = OP_CERT;
    uint32_t n = 8 * 1024 * 1024 + 1;
    CHECK(wr(k.fd, &op, 1) == 0 && wr(k.fd, &n, 4) == 0);
    CHECK(closed(k.fd));
    hangup(&k);
    // A message cut off half way: the client goes away cleanly.
    k = conn();
    n = 100;
    CHECK(wr(k.fd, &op, 1) == 0 && wr(k.fd, &n, 4) == 0 && wr(k.fd, "half", 4) == 0);
    shutdown(k.fd, SHUT_WR);
    CHECK(closed(k.fd));
    hangup(&k);
    // Sessions run out at MAXS; closing one frees its slot.
    k = conn();
    uint8_t sid[MAXS][68];
    uint32_t sl[MAXS];
    for (int i = 0; i < MAXS; i++) CHECK((sl[i] = open_session(k.fd, sid[i])) == 12);
    CHECK(call(k.fd, OP_OPEN, NULL, 0, NULL, NULL) == -2);
    CHECK(call(k.fd, OP_CLOSE, sid[3], sl[3], NULL, NULL) == 0);
    uint8_t again[68];
    CHECK(open_session(k.fd, again) == 12);
    hangup(&k); // the rest are closed by client() itself: LSan/valgrind check that
    // No DRM at all: every request gets -1.
    fake.no_drm = 1;
    k = conn();
    CHECK(call(k.fd, OP_OPEN, NULL, 0, NULL, NULL) == -1);
    hangup(&k);
    fake.no_drm = 0;
}

// Opens a session and gives it a licence with two content keys.
static void licensed(int fd, uint8_t *sid, uint32_t *sl) {
    *sl = open_session(fd, sid);
    uint8_t req[64];
    uint32_t rl = sizeof req;
    CHECK(call(fd, OP_REQUEST, sid, *sl, req, &rl) == 0);
    CHECK(rl == sizeof "fake request");
    PB m;
    licence(&m, 2, 1);
    uint8_t up[8300];
    memcpy(up, sid, *sl);
    memcpy(up + *sl, m.b, m.n);
    uint8_t ids[64];
    uint32_t il = sizeof ids;
    CHECK(call(fd, OP_UPDATE, up, *sl + (uint32_t)m.n, ids, &il) == 0);
    CHECK(il == 32);
    CHECK(ids[0] == 1 && ids[16] == 2);
}

// Two clients taking licences at once: each parse has its own state.
static void *licence_loop(void *a) {
    (void)a;
    Conn k = conn();
    for (int i = 0; i < 40; i++) {
        uint8_t sid[68];
        uint32_t sl;
        licensed(k.fd, sid, &sl);
        CHECK(call(k.fd, OP_CLOSE, sid, sl, NULL, NULL) == 0);
    }
    hangup(&k);
    return NULL;
}
static void test_licence_concurrent(void) {
    pthread_t t[2];
    for (int i = 0; i < 2; i++) pthread_create(&t[i], NULL, licence_loop, NULL);
    for (int i = 0; i < 2; i++) pthread_join(t[i], NULL);
}

static uint32_t mk_sample(uint8_t *b, uint32_t video, uint32_t scheme, uint32_t nsub,
                          uint32_t size) {
    Smp h;
    memset(&h, 0, sizeof h);
    h.video = video;
    h.scheme = scheme;
    h.size = size;
    h.nsub = nsub;
    h.ts = 40000;
    memcpy(b, &h, sizeof h);
    for (uint32_t i = 0; i < nsub; i++) {
        uint32_t cl = size / nsub / 2, en = size / nsub - cl;
        memcpy(b + sizeof h + 8 * i, &cl, 4);
        memcpy(b + sizeof h + 8 * i + 4, &en, 4);
    }
    memset(b + sizeof h + 8 * nsub, 0xab, size);
    return (uint32_t)sizeof h + 8 * nsub + size;
}

static void test_audio_decode(void) {
    Conn k = conn();
    uint8_t sid[68];
    uint32_t sl;
    licensed(k.fd, sid, &sl);
    Init ai = {0, 2, 48000};
    CHECK(call(k.fd, OP_AINIT, &ai, sizeof ai, NULL, NULL) == 0);
    static uint8_t b[64 * 1024], out[16 * 1024];
    fake.audio_out = 3;
    fake.audio_out_size = 1024;
    uint32_t n = mk_sample(b, 0, 1, 2, 1000);
    uint32_t ol = sizeof out;
    CHECK(call(k.fd, OP_SAMPLE, b, n, out, &ol) == 0);
    CHECK(ol == 3 * (16 + 1024)); // [ts][size][pcm] per decoder output
    int64_t ts, sz;
    memcpy(&ts, out, 8);
    memcpy(&sz, out + 8, 8);
    CHECK(ts == 1000 && sz == 1024);
    CHECK(fake.last_n == 2 && fake.last_mode == AMEDIACODECRYPTOINFO_MODE_AES_CTR);
    CHECK(fake.last_cl0 == 250 && fake.last_en0 == 250);
    // No subsamples: the whole sample is encrypted.
    n = mk_sample(b, 0, 1, 0, 600);
    ol = sizeof out;
    CHECK(call(k.fd, OP_SAMPLE, b, n, out, &ol) == 0);
    CHECK(fake.last_n == 1 && fake.last_cl0 == 0 && fake.last_en0 == 600);
    // Bigger than the codec's input buffer: refused, nothing queued.
    fake.in_cap = 100;
    int q = fake.queued;
    ol = sizeof out;
    CHECK(call(k.fd, OP_SAMPLE, b, n, out, &ol) == -7);
    CHECK(fake.queued == q);
    fake.in_cap = 0;
    // Codec busy.
    fake.in_busy = 1;
    CHECK(call(k.fd, OP_SAMPLE, b, n, out, &ol) == ST_BUSY);
    fake.in_busy = 0;
    fake.audio_out = 0;
    hangup(&k);
}

static void test_decrypt_only(void) {
    Conn k = conn();
    uint8_t sid[68];
    uint32_t sl;
    licensed(k.fd, sid, &sl);
    static uint8_t b[8192], out[8192];
    fake.audio_out = 1;
    fake.audio_out_size = 512;
    uint32_t n = mk_sample(b, 2, 2, 1, 512);
    uint32_t ol = sizeof out;
    CHECK(call(k.fd, OP_SAMPLE, b, n, out, &ol) == 0);
    CHECK(ol == 512);
    CHECK(fake.last_mode == AMEDIACODECRYPTOINFO_MODE_AES_CBC);
    fake.audio_out = 0;
    hangup(&k);
}

static void test_video_queue(void) {
    Conn k = conn();
    uint8_t sid[68];
    uint32_t sl;
    licensed(k.fd, sid, &sl);
    Init vi = {1, 1280, 720};
    CHECK(call(k.fd, OP_VINIT, &vi, sizeof vi, NULL, NULL) == 0);
    static uint8_t b[64 * 1024];
    for (int i = 0; i < 30; i++) {
        uint32_t n = mk_sample(b, 1, 1, 4, 4000);
        CHECK(call(k.fd, OP_SAMPLE, b, n, NULL, NULL) == 0);
    }
    uint32_t v = 1;
    CHECK(call(k.fd, OP_RESET, &v, 4, NULL, NULL) == 0);
    v = 0;
    CHECK(call(k.fd, OP_RESET, &v, 4, NULL, NULL) == 0);
    for (int i = 0; i < 5; i++) {
        uint32_t n = mk_sample(b, 1, 1, 1, 2000);
        CHECK(call(k.fd, OP_SAMPLE, b, n, NULL, NULL) == 0);
    }
    // Closing the active session stops the decoder thread.
    CHECK(call(k.fd, OP_CLOSE, sid, sl, NULL, NULL) == 0);
    hangup(&k);
    // Hanging up mid-stream also stops it, with samples still queued.
    k = conn();
    licensed(k.fd, sid, &sl);
    CHECK(call(k.fd, OP_VINIT, &vi, sizeof vi, NULL, NULL) == 0);
    fake.in_busy = 1; // keep them queued
    for (int i = 0; i < 10; i++) {
        uint32_t n = mk_sample(b, 1, 1, 2, 3000);
        CHECK(call(k.fd, OP_SAMPLE, b, n, NULL, NULL) == 0);
    }
    hangup(&k);
    fake.in_busy = 0;
}

// ---- the compositor message ----
static int recv_frame(int s, VF *f, int *fds, int *nfd) {
    char cb[CMSG_SPACE(sizeof(int) * 32)];
    struct iovec io = {f, sizeof *f};
    struct msghdr m;
    memset(&m, 0, sizeof m);
    m.msg_iov = &io;
    m.msg_iovlen = 1;
    m.msg_control = cb;
    m.msg_controllen = sizeof cb;
    ssize_t r = recvmsg(s, &m, MSG_DONTWAIT);
    *nfd = 0;
    if (r < 0) return -1;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            *nfd = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            memcpy(fds, CMSG_DATA(c), sizeof(int) * (size_t)*nfd);
        }
    return (int)r;
}

static void test_send_frame(void) {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
    C *c = calloc(1, sizeof *c);
    c->sk = sv[0];
    c->vi.a = 1280;
    c->vi.b = 720;
    // An AHardwareBuffer whose ANativeWindowBuffer header sits at offset 0.
    static union {
        ANWB an;
        uint8_t raw[256];
    } hb;
    // An NH with room for 101 data ints after its header.
    static int nhraw[sizeof(NH) / sizeof(int) + 101];
    NH *nh = (NH *)(void *)nhraw;
    memset(&hb, 0, sizeof hb);
    hb.an.magic = 0x5f626672;
    hb.an.width = 1280;
    hb.an.height = 736;
    hb.an.stride = 1280;
    hb.an.handle = nh;
    int p[2];
    CHECK(pipe(p) == 0);
    nh->numFds = 1;
    nh->numInts = 4;
    nh->data[0] = p[0];
    for (int i = 1; i <= 4; i++) nh->data[i] = 100 + i;
    fake.image_buffer = &hb;
    AImage *im = (AImage *)(void *)&hb; // only passed through to the fake
    send_frame(c, im, p[1]);
    VF f;
    int fds[32], nfd;
    CHECK(recv_frame(sv[1], &f, fds, &nfd) == (int)sizeof f);
    CHECK(nfd == 2); // the buffer fd and the fence
    CHECK(f.width == 1280 && f.height == 736 && f.cropH == 720);
    CHECK(f.numFds == 1 && f.numInts == 4 && f.hasFence == 1);
    CHECK(f.ints[0] == 101 && f.ints[3] == 104);
    for (int i = 0; i < nfd; i++) close(fds[i]);
    // Handles too big for the message are dropped, not copied.
    nh->numInts = 97;
    send_frame(c, im, -1);
    CHECK(recv_frame(sv[1], &f, fds, &nfd) < 0 && errno == EAGAIN);
    nh->numInts = 4;
    nh->numFds = 32;
    send_frame(c, im, -1);
    CHECK(recv_frame(sv[1], &f, fds, &nfd) < 0 && errno == EAGAIN);
    nh->numFds = -1;
    send_frame(c, im, -1);
    CHECK(recv_frame(sv[1], &f, fds, &nfd) < 0 && errno == EAGAIN);
    // The largest that fits still goes through.
    nh->numFds = 1;
    nh->numInts = 96;
    send_frame(c, im, -1);
    CHECK(recv_frame(sv[1], &f, fds, &nfd) == (int)sizeof f && nfd == 1);
    for (int i = 0; i < nfd; i++) close(fds[i]);
    fake.image_buffer = NULL;
    close(p[0]);
    close(p[1]);
    close(sv[0]);
    close(sv[1]);
    free(c);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    // wvbridged's own status lines are not what is being tested.
    if (!getenv("WV_VERBOSE")) {
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) {
            dup2(null, 1);
            close(null);
        }
    }
    static const struct {
        const char *name;
        void (*fn)(void);
    } t[] = {
        {"licence_parse", test_licence_parse},
        {"licence_key_cap", test_licence_key_cap},
        {"licence_bad_input", test_licence_bad_input},
        {"licence_fuzz", test_licence_fuzz},
        {"protocol_malformed", test_protocol_malformed},
        {"protocol_limits", test_protocol_limits},
        {"licence_concurrent", test_licence_concurrent},
        {"audio_decode", test_audio_decode},
        {"decrypt_only", test_decrypt_only},
        {"video_queue", test_video_queue},
        {"send_frame", test_send_frame},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        int before = failed;
        t[i].fn();
        fprintf(stderr, "%-20s %s\n", t[i].name, failed == before ? "ok" : "FAILED");
    }
    fprintf(stderr, "%s\n", failed ? "FAILED" : "all passed");
    return failed != 0;
}
