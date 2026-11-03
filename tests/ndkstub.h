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

// Bionic's macros used by the NDK media headers, so they parse with GCC
// against glibc for the host tests.
#pragma once
#define __INTRODUCED_IN(x)
#define __DEPRECATED_IN(x)
#define __REMOVED_IN(x)
#define __INTRODUCED_IN_NO_GUARD_FOR_NDK(x)
#define __ANDROID_API__ 29
#define __ANDROID_API_FUTURE__ 10000
#define __BEGIN_DECLS
#define __END_DECLS
#define _Nullable
#define _Nonnull
