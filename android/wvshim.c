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

// Preloaded into the Widevine HAL. A timed wait whose deadline has already
// passed returns at once without entering the kernel, so a caller looping on
// one spins. Give it a short real sleep, with its mutex released as a real
// wait would. Everything else goes straight through.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
static int (*real_wait)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
static long expired, normal;
static void logline(const struct timespec *a, const struct timespec *n, const void *ra) {
    FILE *f = fopen("/data/local/tmp/wv/shim.log", "a");
    if (!f) return;
    fprintf(f, "tid %d expired=%ld normal=%ld deadline %lld.%09ld now %lld.%09ld caller %p\n",
            gettid(), expired, normal, (long long)a->tv_sec, a->tv_nsec, (long long)n->tv_sec,
            n->tv_nsec, ra);
    fclose(f);
}
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *a) {
    if (!real_wait) *(void **)&real_wait = dlsym(RTLD_NEXT, "pthread_cond_timedwait");
    struct timespec before;
    clock_gettime(CLOCK_REALTIME, &before);
    int r = real_wait(c, m, a);
    if (r == ETIMEDOUT && (a->tv_sec < before.tv_sec ||
                           (a->tv_sec == before.tv_sec && a->tv_nsec <= before.tv_nsec))) {
        long e = __atomic_add_fetch(&expired, 1, __ATOMIC_RELAXED);
        if (e <= 4 || e % 5000 == 0) logline(a, &before, __builtin_return_address(0));
        pthread_mutex_unlock(m);
        struct timespec s = {0, SLEEP_NS};
        nanosleep(&s, NULL);
        pthread_mutex_lock(m);
    } else {
        __atomic_add_fetch(&normal, 1, __ATOMIC_RELAXED);
    }
    return r;
}
