/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/SpawnedProcess.h"

#include <folly/String.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>
#include <list>

#include "eden/common/utils/PathFuncs.h"

#ifdef __APPLE__
#include <dlfcn.h>
#endif

using namespace facebook::eden;
using Options = SpawnedProcess::Options;

#ifndef _WIN32
TEST(SpawnedProcess, computeSpawnFlagsAddsResetIdsWhenRequested) {
  Options defaultOpts;
  EXPECT_EQ(
      POSIX_SPAWN_SETSIGDEF, SpawnedProcess::computeSpawnFlags(defaultOpts));

  Options resetIdsOpts;
  resetIdsOpts.resetIds();
  EXPECT_EQ(
      POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_RESETIDS,
      SpawnedProcess::computeSpawnFlags(resetIdsOpts));
}

TEST(SpawnedProcess, cwd_slash) {
  Options opts;
  opts.nullStdin();
  opts.pipeStdout();
  opts.chdir(kRootAbsPath);
  SpawnedProcess proc({"pwd"}, std::move(opts));

  auto outputs = proc.communicate();
  proc.wait();

  EXPECT_EQ("/\n", outputs.first);
}

TEST(SpawnedProcess, cwd_inherit) {
  Options opts;
  opts.nullStdin();
  opts.pipeStdout();
  SpawnedProcess proc({"pwd"}, std::move(opts));

  auto outputs = proc.communicate();
  proc.wait();

  auto stdout = outputs.first;

  EXPECT_FALSE(stdout.empty());
  EXPECT_EQ('\n', stdout[stdout.size() - 1]);
  stdout = stdout.substr(0, stdout.size() - 1);

  char cwd[1024];
  getcwd(cwd, sizeof(cwd) - 1);

  EXPECT_EQ(realpath(cwd), realpath(stdout));
}
#endif

TEST(SpawnedProcess, pipe) {
  Options opts;
  opts.nullStdin();
  opts.pipeStdout();
  SpawnedProcess echo(
      {
#ifndef _WIN32
          "echo",
#else
          "powershell",
          "-Command",
          "echo",
#endif
          "hello"},
      std::move(opts));

  auto outputs = echo.communicate();
  echo.wait();

  folly::StringPiece line(outputs.first);
  EXPECT_EQ(line.subpiece(0, 5), "hello");
}

void test_pipe_input(bool threaded) {
#ifndef _WIN32
  Options opts;
  opts.pipeStdout();
  opts.pipeStdin();
  SpawnedProcess cat({"cat", "-"}, std::move(opts));

  std::vector<std::string> expected{"one", "two", "three"};
  std::list<std::string> lines{"one\n", "two\n", "three\n"};

  auto writable = [&lines](FileDescriptor& fd) {
    if (lines.empty()) {
      return true;
    }
    auto str = lines.front();
    if (write(fd.fd(), str.data(), str.size()) == -1) {
      throw std::runtime_error("write to child failed");
    }
    lines.pop_front();
    return false;
  };

  auto outputs =
      threaded ? cat.threadedCommunicate(writable) : cat.communicate(writable);
  cat.wait();

  std::vector<std::string> resultLines;
  folly::split('\n', outputs.first, resultLines, /*ignoreEmpty=*/true);
  EXPECT_EQ(resultLines.size(), 3);
  EXPECT_EQ(resultLines, expected);
#else
  (void)threaded;
#endif
}

TEST(SpawnedProcess, stresstest_pipe_output) {
  bool okay = true;
#ifndef _WIN32
  for (int i = 0; i < 3000; ++i) {
    Options opts;
    opts.pipeStdout();
    opts.nullStdin();
    SpawnedProcess proc({"head", "-n20", "/dev/urandom"}, std::move(opts));
    auto outputs = proc.communicate();
    folly::StringPiece out(outputs.first);
    proc.wait();
    if (out.empty() || out[out.size() - 1] != '\n') {
      okay = false;
      break;
    }
  }
#endif
  EXPECT_TRUE(okay);
}

TEST(SpawnedProcess, inputThreaded) {
  test_pipe_input(true);
}

TEST(SpawnedProcess, inputNotThreaded) {
  test_pipe_input(false);
}

#ifdef __APPLE__
TEST(SpawnedProcess, disclaimTccResponsibility) {
  // Both APIs used here are private, with no public headers, so resolve them
  // at runtime and skip the test if either is unavailable.
  using GetResponsiblePidFn = pid_t (*)(pid_t);
  auto getResponsiblePid = reinterpret_cast<GetResponsiblePidFn>(
      dlsym(RTLD_DEFAULT, "responsibility_get_pid_responsible_for_pid"));
  auto setDisclaim =
      dlsym(RTLD_DEFAULT, "responsibility_spawnattrs_setdisclaim");
  if (!getResponsiblePid || !setDisclaim) {
    GTEST_SKIP() << "responsibility APIs are unavailable on this system";
  }
  // On hosts where the proc_info lookup behind this API is restricted it
  // returns -1; skip rather than failing the comparisons below.
  if (getResponsiblePid(getpid()) == -1) {
    GTEST_SKIP()
        << "responsibility_get_pid_responsible_for_pid is restricted here";
  }

  auto spawnSleep = [](bool disclaim) {
    Options opts;
    opts.nullStdin();
    if (disclaim) {
      opts.disclaimTccResponsibility();
    }
    // The child must still be alive when we query its responsible pid,
    // so spawn something that sleeps until we kill it.
    return SpawnedProcess({"/bin/sleep", "30"}, std::move(opts));
  };

  auto disclaimed = spawnSleep(/*disclaim=*/true);
  EXPECT_EQ(disclaimed.pid(), getResponsiblePid(disclaimed.pid()));
  disclaimed.kill();
  disclaimed.wait();

  auto inherited = spawnSleep(/*disclaim=*/false);
  EXPECT_NE(inherited.pid(), getResponsiblePid(inherited.pid()));
  inherited.kill();
  inherited.wait();
}
#endif

TEST(SpawnedProcess, shellQuoting) {
  std::vector<std::string> args;
  if (folly::kIsWindows) {
    args.emplace_back("powershell");
    args.emplace_back("-Command");
  } else {
    args.emplace_back("/bin/sh");
    args.emplace_back("-c");
  }

  args.emplace_back("echo \"This is a test\"");

  Options opts;
  opts.nullStdin();
  opts.pipeStdout();
  SpawnedProcess proc(args, std::move(opts));
  auto outputs = proc.communicate();

  auto status = proc.wait();
  EXPECT_EQ(status.exitStatus(), 0);

  folly::StringPiece line(outputs.first);
  EXPECT_EQ(line.subpiece(0, 14), "This is a test");
}
