/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <string>

#ifdef __APPLE__
// Fetches the value of a sysctl by name.
// The result is assumed to be a string.
std::string getSysCtlByName(const char* name, size_t size);
#endif
