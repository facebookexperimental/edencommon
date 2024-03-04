/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "eden/common/utils/FileDescriptor.h"

namespace facebook::eden {

struct Pipe {
  FileDescriptor read;
  FileDescriptor write;

  explicit Pipe(bool nonBlocking = false);
};

struct SocketPair {
  FileDescriptor read;
  FileDescriptor write;

  explicit SocketPair(bool nonBlocking = false);
};

} // namespace facebook::eden
