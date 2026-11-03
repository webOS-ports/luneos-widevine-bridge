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

# Host tests for android/wvbridged.c, against a fake NDK (fake_ndk.c) and the
# NDK's own media headers:
#   make NDK=/path/to/android-ndk check        plain build
#   make NDK=... asan | ubsan | tsan | valgrind
#   make NDK=... all-checks                    every one of the above
# CC=clang works as well.
NDK ?= $(error set NDK=/path/to/android-ndk)
CC ?= cc
B ?= build
SYSINC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include
CFLAGS ?= -O1 -g
WARN = -Wall -Wextra -Wshadow -Wformat=2 -Wconversion -Wno-sign-conversion
CPPFLAGS += -D_GNU_SOURCE -include ndkstub.h -I$(B)/ndkinc -I. -I..
SRC = wvbridged_test.c fake_ndk.c
DEPS = $(SRC) fake_ndk.h ndkstub.h ../android/wvbridged.c ../proto.h $(B)/ndkinc/.stamp

# The NDK's media/ and android/ headers, minus api-level.h (bionic only).
$(B)/ndkinc/.stamp:
	mkdir -p $(B)/ndkinc/android
	ln -sfn $(SYSINC)/media $(B)/ndkinc/media
	for h in $(SYSINC)/android/*.h; do ln -sf $$h $(B)/ndkinc/android/; done
	rm -f $(B)/ndkinc/android/api-level.h
	printf '#pragma once\n' > $(B)/ndkinc/android/api-level.h
	touch $@

$(B)/test: $(DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(SRC) -o $@ -pthread
$(B)/test-asan: $(DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer $(SRC) -o $@ -pthread
$(B)/test-tsan: $(DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -fsanitize=thread $(SRC) -o $@ -pthread

check: $(B)/test
	$(B)/test
asan ubsan: $(B)/test-asan
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 $(B)/test-asan
tsan: $(B)/test-tsan
	TSAN_OPTIONS=halt_on_error=1 $(B)/test-tsan
valgrind: $(B)/test
	WV_VERBOSE=1 WV_FUZZ_ITERS=2000 valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=definite,indirect \
		--errors-for-leak-kinds=definite,indirect --track-fds=yes $(B)/test >/dev/null
all-checks: check asan tsan valgrind

clean:
	rm -rf $(B)

.PHONY: check asan ubsan tsan valgrind all-checks clean
