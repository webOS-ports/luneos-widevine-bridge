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

# The LuneOS half: the library CDM Chromium loads. Built with the image's
# toolchain (glibc). The Android half is built into a Halium GSI (Android.bp)
# or with the NDK (android/build.sh).

CXX ?= g++
CXXFLAGS ?= -O2
CDM_FLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -fPIC -shared \
            -fno-exceptions -fno-rtti -fvisibility=hidden \
            -Ithird_party/cdm -I.

all: libwidevinecdm.so

libwidevinecdm.so: cdm/wvbridgecdm.cc proto.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CDM_FLAGS) $< -o $@ $(LDFLAGS) \
		-static-libstdc++ -static-libgcc -Wl,--as-needed

clean:
	rm -f libwidevinecdm.so

.PHONY: all clean
