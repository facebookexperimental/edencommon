/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef _WIN32

#include "eden/common/utils/MappedDiskVector.h"

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#endif

#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>
#include <folly/testing/TestUtil.h>

using facebook::eden::MappedDiskVector;
using folly::test::TemporaryDirectory;

TEST(MappedDiskVector, roundUpToNonzeroPageSize) {
  using namespace facebook::eden::detail;
  EXPECT_EQ(kPageSize, roundUpToNonzeroPageSize(0));
  EXPECT_EQ(kPageSize, roundUpToNonzeroPageSize(1));
  EXPECT_EQ(kPageSize, roundUpToNonzeroPageSize(kPageSize - 1));
  EXPECT_EQ(kPageSize, roundUpToNonzeroPageSize(kPageSize));
  EXPECT_EQ(kPageSize * 2, roundUpToNonzeroPageSize(kPageSize + 1));
  EXPECT_EQ(kPageSize * 2, roundUpToNonzeroPageSize(kPageSize * 2 - 1));
  EXPECT_EQ(kPageSize * 2, roundUpToNonzeroPageSize(kPageSize * 2));
}

namespace {
struct MappedDiskVectorTest : ::testing::Test {
  MappedDiskVectorTest()
      : tmpDir{"eden_mdv_"}, mdvPath{(tmpDir.path() / "test.mdv").string()} {}
  TemporaryDirectory tmpDir;
  std::string mdvPath;
};

struct U64 {
  enum { VERSION = 0 };

  /* implicit */ U64(uint64_t v) : value{v} {}
  operator uint64_t() const {
    return value;
  }
  uint64_t value;
};
} // namespace

TEST_F(MappedDiskVectorTest, grows_file) {
  auto mdv = MappedDiskVector<U64>::open(mdvPath);
  EXPECT_EQ(0, mdv.size());

  struct stat st;
  ASSERT_EQ(0, stat(mdvPath.c_str(), &st));
  auto old_size = st.st_size;

  // 8 MB
  constexpr uint64_t N = 1000000;
  for (uint64_t i = 0; i < N; ++i) {
    mdv.emplace_back(i);
  }
  EXPECT_EQ(N, mdv.size());

  ASSERT_EQ(0, stat(mdvPath.c_str(), &st));
  auto new_size = st.st_size;
  EXPECT_GT(new_size, old_size);
}

TEST_F(MappedDiskVectorTest, remembers_contents_on_reopen) {
  {
    auto mdv = MappedDiskVector<U64>::open(mdvPath);
    mdv.emplace_back(15ull);
    mdv.emplace_back(25ull);
    mdv.emplace_back(35ull);
  }

  auto mdv = MappedDiskVector<U64>::open(mdvPath);
  EXPECT_EQ(3, mdv.size());
  EXPECT_EQ(15, mdv[0]);
  EXPECT_EQ(25, mdv[1]);
  EXPECT_EQ(35, mdv[2]);
}

TEST_F(MappedDiskVectorTest, pop_back) {
  auto mdv = MappedDiskVector<U64>::open(mdvPath);
  mdv.emplace_back(1ull);
  mdv.emplace_back(2ull);
  mdv.pop_back();
  mdv.emplace_back(3ull);
  EXPECT_EQ(2, mdv.size());
  EXPECT_EQ(1, mdv[0]);
  EXPECT_EQ(3, mdv[1]);
}

namespace {
struct Small {
  enum { VERSION = 0 };
  unsigned x;
};
struct Large {
  enum { VERSION = 0 };
  unsigned x;
  unsigned y;
};
struct SmallNew {
  enum { VERSION = 1 };
  unsigned x;
};
} // namespace

TEST_F(MappedDiskVectorTest, throws_if_size_does_not_match) {
  {
    auto mdv = MappedDiskVector<Small>::open(mdvPath);
    mdv.emplace_back(Small{1});
  }

  try {
    auto mdv = MappedDiskVector<Large>::open(mdvPath);
    FAIL() << "MappedDiskVector didn't throw";
  } catch (const std::runtime_error& e) {
    EXPECT_EQ(
        "Record size does not match size recorded in file. "
        "Expected 8 but file has 4",
        std::string(e.what()));
  } catch (const std::exception& e) {
    FAIL() << "Unexpected exception: " << e.what();
  }
}

TEST_F(MappedDiskVectorTest, throws_if_version_does_not_match) {
  {
    auto mdv = MappedDiskVector<Small>::open(mdvPath);
    mdv.emplace_back(Small{1});
  }

  try {
    auto mdv = MappedDiskVector<SmallNew>::open(mdvPath);
    FAIL() << "MappedDiskVector didn't throw";
  } catch (const std::runtime_error& e) {
    EXPECT_EQ(
        "Unexpected record size and version. "
        "Expected size=4, version=1 but got size=4, version=0",
        std::string(e.what()));
  } catch (const std::exception& e) {
    FAIL() << "Unexpected exception: " << e.what();
  }
}

namespace {
struct Old {
  enum { VERSION = 0 };
  unsigned x;
};
struct New {
  enum { VERSION = 1 };
  explicit New(const Old& old) : x(-old.x), y(old.x) {}
  unsigned x;
  unsigned y;
};
} // namespace

TEST_F(MappedDiskVectorTest, migrates_from_old_version_to_new) {
  {
    auto mdv = MappedDiskVector<Old>::open(mdvPath);
    mdv.emplace_back(Old{1});
    mdv.emplace_back(Old{2});
  }

  {
    auto mdv = MappedDiskVector<New>::open<Old>(mdvPath);
    EXPECT_EQ(2, mdv.size());
    EXPECT_EQ(-1, mdv[0].x);
    EXPECT_EQ(1, mdv[0].y);
    EXPECT_EQ(-2, mdv[1].x);
    EXPECT_EQ(2, mdv[1].y);
  }

  // and moves the new database over the old one
  {
    auto mdv = MappedDiskVector<New>::open(mdvPath);
    EXPECT_EQ(2, mdv.size());
    EXPECT_EQ(-1, mdv[0].x);
    EXPECT_EQ(1, mdv[0].y);
    EXPECT_EQ(-2, mdv[1].x);
    EXPECT_EQ(2, mdv[1].y);
  }
}

namespace {
struct V1 {
  enum { VERSION = 1 };
  uint8_t value;
  uint8_t conversionCount{0};
};
struct V2 {
  enum { VERSION = 2 };
  explicit V2(V1 old)
      : value(old.value), conversionCount(old.conversionCount + 1) {}
  uint16_t value;
  uint16_t conversionCount{0};
};
struct V3 {
  enum { VERSION = 3 };
  explicit V3(V2 old)
      : value(old.value), conversionCount(old.conversionCount + 1) {}
  uint32_t value;
  uint32_t conversionCount{0};
};
struct V4 {
  enum { VERSION = 4 };
  explicit V4(V3 old)
      : value(old.value), conversionCount(old.conversionCount + 1) {}
  uint64_t value;
  uint64_t conversionCount{0};
};
} // namespace

TEST_F(MappedDiskVectorTest, migrates_across_multiple_versions) {
  {
    auto mdv = MappedDiskVector<V1>::open(mdvPath);
    mdv.emplace_back(V1{1});
    mdv.emplace_back(V1{2});
  }

  {
    auto mdv = MappedDiskVector<V4>::open<V3, V2, V1>(mdvPath);
    EXPECT_EQ(1, mdv[0].value);
    EXPECT_EQ(3, mdv[0].conversionCount);
    EXPECT_EQ(2, mdv[1].value);
    EXPECT_EQ(3, mdv[1].conversionCount);
  }
}

#ifdef __linux__
namespace {
// Set up uid/gid mapping after unshare(CLONE_NEWUSER). Must pass the
// original uid/gid from BEFORE the unshare, since getuid()/getgid()
// return the overflow IDs (65534) inside the new namespace.
bool setupIdMaps(uid_t realUid, gid_t realGid) {
  auto writeFile = [](const char* path, const char* content) -> bool {
    int fd = ::open(path, O_WRONLY);
    if (fd < 0) {
      return false;
    }
    bool ok = ::write(fd, content, strlen(content)) > 0;
    ::close(fd);
    return ok;
  };

  char buf[64];

  snprintf(buf, sizeof(buf), "0 %d 1\n", realUid);
  if (!writeFile("/proc/self/uid_map", buf)) {
    return false;
  }

  // Must deny setgroups before writing gid_map.
  if (!writeFile("/proc/self/setgroups", "deny\n")) {
    return false;
  }

  snprintf(buf, sizeof(buf), "0 %d 1\n", realGid);
  return writeFile("/proc/self/gid_map", buf);
}

// Check whether we can create a user+mount namespace (needed for the ENOSPC
// test). We fork a child to test this so we don't affect the parent.
bool canMountTmpfs() {
  uid_t uid = getuid();
  gid_t gid = getgid();
  pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
      _exit(1);
    }
    if (!setupIdMaps(uid, gid)) {
      _exit(1);
    }
    char dir[] = "/tmp/mdv_probe_XXXXXX";
    if (!mkdtemp(dir)) {
      _exit(1);
    }
    int rc = mount("tmpfs", dir, "tmpfs", 0, "size=4k");
    if (rc == 0) {
      umount(dir);
    }
    rmdir(dir);
    _exit(rc == 0 ? 0 : 1);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
// Install a seccomp-bpf filter that makes the fallocate syscall return
// EOPNOTSUPP, forcing extendFile to fall back to ftruncate. This is the
// only reliable way to create sparse pages in a unit test. In production,
// the trigger is different: fallocate succeeds but pages are unwritable
// (btrfs data/metadata space split, thin-provisioned LVM pool exhaustion,
// overcommitted tmpfs, NFS server out of space).
bool blockFallocate() {
  // BPF program that intercepts every syscall and blocks fallocate:
  //   1. Load the syscall number into the accumulator
  //   2. If it's fallocate, jump to step 3; otherwise jump to step 4
  //   3. Return EOPNOTSUPP — the kernel fails the syscall without executing it
  //   4. Allow all other syscalls through
  struct sock_filter filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_fallocate, 0, 1),
      BPF_STMT(
          BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EOPNOTSUPP & SECCOMP_RET_DATA)),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  struct sock_fprog prog = {
      .len = sizeof(filter) / sizeof(filter[0]),
      .filter = filter,
  };
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == 0;
}
} // namespace

TEST_F(MappedDiskVectorTest, emplace_back_throws_on_enospc) {
  if (!canMountTmpfs()) {
    GTEST_SKIP() << "User namespaces / tmpfs mount not available";
  }

  auto mountpoint = (tmpDir.path() / "enospc").string();
  ::mkdir(mountpoint.c_str(), 0700);

  EXPECT_EXIT(
      {
        signal(SIGBUS, SIG_DFL);

        uid_t uid = getuid();
        gid_t gid = getgid();

        // Create a user+mount namespace so we can mount tmpfs without root.
        if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
          _exit(1);
        }
        if (!setupIdMaps(uid, gid)) {
          _exit(1);
        }

        // Mount a tmpfs exactly the size of the initial MDV file (1MB).
        // This leaves no room for growth.
        if (mount("tmpfs", mountpoint.c_str(), "tmpfs", 0, "size=1048576") !=
            0) {
          _exit(1);
        }

        auto path = mountpoint + "/test.mdv";
        auto mdv = MappedDiskVector<U64>::open(path);

        // Fill to capacity, consuming the entire 1MB tmpfs.
        size_t cap = mdv.capacity();
        for (uint64_t i = 0; i < cap; ++i) {
          mdv.emplace_back(i);
        }

        try {
          mdv.emplace_back(99ull);
          _exit(2); // Should have thrown.
        } catch (const std::system_error&) {
          _exit(0); // Success: clean error instead of SIGBUS.
        }
      },
      testing::ExitedWithCode(0),
      "");
}

TEST_F(MappedDiskVectorTest, populate_throws_on_truncated_file) {
  // Verify that MADV_POPULATE_READ detects I/O errors (e.g. btrfs checksum
  // failures) and throws an exception instead of allowing a SIGBUS later.
  // The callback truncates the file between mmap and population, simulating
  // a corrupted backing store.
  {
    auto mdv = MappedDiskVector<U64>::open(mdvPath);
    // Add enough entries to span multiple pages.
    for (uint64_t i = 0; i < 2048; ++i) {
      mdv.emplace_back(i);
    }
  }

  EXPECT_EXIT(
      {
        signal(SIGBUS, SIG_DFL);

        auto path = mdvPath;
        try {
          auto mdv = MappedDiskVector<U64>::open(
              path,
              /*shouldPopulate=*/true,
              [&]() {
                // Truncate the file to just the header after mmap but before
                // population, simulating a corrupted/truncated backing store.
                int fd = ::open(path.c_str(), O_RDWR);
                if (fd < 0) {
                  _exit(1);
                }
                ftruncate(fd, 32);
                ::close(fd);
              });
          _exit(2); // Should have thrown.
        } catch (const std::system_error&) {
          _exit(0); // Success: clean exception instead of SIGBUS.
        }
      },
      testing::ExitedWithCode(0),
      "");
}

TEST_F(
    MappedDiskVectorTest,
    emplace_back_detects_write_failure_on_grown_pages) {
  // Seccomp simulates the worst case: fallocate blocked -> ftruncate
  // creates guaranteed-sparse pages on a full tmpfs. In production, the
  // trigger is fallocate succeeding while the pages remain unwritable
  // (btrfs data/metadata space split, thin-provisioned LVM, overcommitted
  // tmpfs, NFS server full). MADV_POPULATE_WRITE is the safety net for all
  // of these — it forces the kernel to back each page with write intent,
  // surfacing disk-full as an exception instead of SIGBUS.
  if (!canMountTmpfs()) {
    GTEST_SKIP() << "User namespaces / tmpfs mount not available";
  }

  auto mountpoint = (tmpDir.path() / "growfail").string();
  ::mkdir(mountpoint.c_str(), 0700);

  EXPECT_EXIT(
      {
        signal(SIGBUS, SIG_DFL);

        uid_t uid = getuid();
        gid_t gid = getgid();

        if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
          _exit(1);
        }
        if (!setupIdMaps(uid, gid)) {
          _exit(1);
        }

        // Mount a 1MB tmpfs — exactly the initial MDV file size.
        // No room for growth.
        if (mount("tmpfs", mountpoint.c_str(), "tmpfs", 0, "size=1048576") !=
            0) {
          _exit(1);
        }

        auto path = mountpoint + "/test.mdv";
        auto mdv = MappedDiskVector<U64>::open(path);

        // Fill to capacity so next emplace_back triggers growth.
        size_t cap = mdv.capacity();
        for (uint64_t i = 0; i < cap; ++i) {
          mdv.emplace_back(i);
        }

        // Install seccomp filter to block fallocate AFTER the MDV is
        // created and filled. This forces extendFile to fall back to
        // ftruncate, which creates sparse pages on the full tmpfs.
        if (!blockFallocate()) {
          _exit(1);
        }

        try {
          // FIXME: emplace_back should throw std::system_error, but
          // SIGBUSes because there's no pre-fault before the
          // placement-new write.
          mdv.emplace_back(cap);
          _exit(2); // Should have thrown.
        } catch (const std::system_error&) {
          _exit(0); // Clean error detection.
        }
      },
      // FIXME: Should be ExitedWithCode(0) once pre-fault is added.
      testing::KilledBySignal(SIGBUS),
      "");
}

TEST_F(MappedDiskVectorTest, open_detects_write_failure_on_initial_pages) {
  // Seccomp simulates the worst case for initial pages: fallocate blocked ->
  // ftruncate creates a sparse file on a tiny tmpfs. In production, the
  // trigger is fallocate succeeding while pages remain unwritable (btrfs
  // data/metadata space split, thin provisioning, etc). populateForWrite in
  // emplace_back detects this and throws std::system_error.
  if (!canMountTmpfs()) {
    GTEST_SKIP() << "User namespaces / tmpfs mount not available";
  }

  auto mountpoint = (tmpDir.path() / "initfail").string();
  ::mkdir(mountpoint.c_str(), 0700);

  EXPECT_EXIT(
      {
        signal(SIGBUS, SIG_DFL);

        uid_t uid = getuid();
        gid_t gid = getgid();

        if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
          _exit(1);
        }
        if (!setupIdMaps(uid, gid)) {
          _exit(1);
        }

        // Mount a tiny tmpfs -- enough for the MDV header page but not
        // the full initial file. fallocate is blocked, so ftruncate
        // creates a 1MB sparse file. Only page 0 (header) can be
        // backed; the rest SIGBUS on write.
        if (mount("tmpfs", mountpoint.c_str(), "tmpfs", 0, "size=4096") != 0) {
          _exit(1);
        }

        // Block fallocate BEFORE open, so initializeFromScratch uses
        // ftruncate, creating a 1MB sparse file with no backing pages
        // beyond page 0.
        if (!blockFallocate()) {
          _exit(1);
        }

        auto path = mountpoint + "/test.mdv";
        try {
          auto mdv = MappedDiskVector<U64>::open(path);
          // Fill page 0 (header is 32 bytes, so (4096-32)/8 = 508
          // entries fit on the first page). Entry 508 crosses to page 1
          // which can't be backed on a 4KB tmpfs.
          // FIXME: Should throw std::system_error from populateForWrite
          // in emplace_back, but currently SIGBUSes on page 1.
          for (uint64_t i = 0; i < 600; ++i) {
            mdv.emplace_back(i);
          }
          _exit(2);
        } catch (const std::system_error&) {
          _exit(0);
        }
      },
      // FIXME: Should be ExitedWithCode(0) once per-page pre-fault is added.
      testing::KilledBySignal(SIGBUS),
      "");
}
#endif // __linux__

TEST_F(MappedDiskVectorTest, migrate_overwrites_existing_tmp_file) {
  {
    auto mdv = MappedDiskVector<Old>::open(mdvPath);
    mdv.emplace_back(Old{1});
    mdv.emplace_back(Old{2});
  }

  folly::writeFileAtomic(mdvPath + ".tmp", "junk data");

  {
    auto mdv = MappedDiskVector<New>::open<Old>(mdvPath);
    EXPECT_EQ(2, mdv.size());
    EXPECT_EQ(-1, mdv[0].x);
    EXPECT_EQ(1, mdv[0].y);
    EXPECT_EQ(-2, mdv[1].x);
    EXPECT_EQ(2, mdv[1].y);
  }
}

#endif
