/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Windows headers have to be included before everything else,
// otherwise we get macro definition conflicts through
// transitive includes.
#ifdef _WIN32
#include <windows.h> // @manual
#include <winternl.h> // @manual
#endif

#ifdef ERROR
#undef ERROR // wingdi.h
#endif

#include "eden/common/utils/ProcessInfo.h"
#include "eden/common/utils/UserInfo.h"
#include "eden/common/utils/windows/WinError.h"

#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/lang/ToAscii.h>
#include <folly/logging/xlog.h>
#include "eden/common/utils/Handle.h"
#include "eden/common/utils/StringConv.h"

#include <fstream>
#include <optional>
#include <sstream>

#ifdef __APPLE__
#include <libproc.h> // @manual
#include <sys/proc_info.h> // @manual
#include <sys/sysctl.h> // @manual
#endif

#ifndef _WIN32
#include <pwd.h>
#endif

namespace facebook::eden {

namespace detail {

#ifdef _WIN32
// Microsoft recommends using runtime dynamic linking for applications
// that want to use `NtQueryInformationProcess`.
// https://docs.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess
//
// This is a simple RAII wrapper for linking `ntdll.dll` at runtime.
class DynamicallyLinkedLibrary {
 public:
  explicit DynamicallyLinkedLibrary(const char* name)
      : handle_(LoadLibraryA(name)) {}

  template <class T>
  T getProcAddress(const char* procName) {
    if (handle_ != NULL) {
      return (T)GetProcAddress(handle_, procName);
    }
    return nullptr;
  }

  ~DynamicallyLinkedLibrary() {
    if (handle_ != NULL) {
      FreeLibrary(handle_);
    }
  }

 private:
  HMODULE handle_ = NULL;
};

std::optional<std::string> getProcessCommandLine(pid_t pid) {
  static DynamicallyLinkedLibrary ntdll("ntdll.dll");
  static auto queryProcessInformation =
      ntdll.getProcAddress<decltype(&NtQueryInformationProcess)>(
          "NtQueryInformationProcess");

  ProcessHandle process{
      OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid)};

  if (!process) {
    return std::nullopt;
  }

  PROCESS_BASIC_INFORMATION processInfo{};

  {
    ULONG readLength{};
    if (NTSTATUS status = queryProcessInformation(
            process.get(),
            ProcessBasicInformation,
            &processInfo,
            sizeof(processInfo),
            &readLength);
        !NT_SUCCESS(status)) {
      return fmt::format(
          FMT_STRING("NtQueryInformationProcess failed with {}"), status);
    }

    // This has never happened during testing.
    // Technically we could hit this if the layout of PROCESS_BASIC_INFORMATION
    // changes.
    if (!processInfo.PebBaseAddress) {
      return "<err:PEB is null>";
    }
  }

  // The information we need is a couple pointers away
  // from PEB, so we'll need several calls to `ReadProcessMemory`.
  PEB peb{};

  if (SIZE_T numBytesRead{}; !ReadProcessMemory(
          process.get(),
          processInfo.PebBaseAddress,
          &peb,
          sizeof(peb),
          &numBytesRead)) {
    auto err = GetLastError();
    return fmt::format(
        FMT_STRING("<ReadProcessMemory err:{}>"), win32ErrorToString(err));
  }

  RTL_USER_PROCESS_PARAMETERS userProcessParams{};

  if (SIZE_T numBytesRead{}; !ReadProcessMemory(
          process.get(),
          peb.ProcessParameters,
          &userProcessParams,
          sizeof(userProcessParams),
          &numBytesRead)) {
    auto err = GetLastError();
    return fmt::format(
        FMT_STRING("<ReadProcessMemory err:{}>"), win32ErrorToString(err));
  }

  std::wstring cmd;
  // `Length` is in bytes, not including the terminating character
  // https://docs.microsoft.com/en-us/windows/win32/api/subauth/ns-subauth-unicode_string
  cmd.resize(userProcessParams.CommandLine.Length / sizeof(WCHAR));
  static_assert(sizeof(wchar_t) == sizeof(WCHAR));

  if (SIZE_T numBytesRead{}; !ReadProcessMemory(
          process.get(),
          userProcessParams.CommandLine.Buffer,
          &cmd[0],
          userProcessParams.CommandLine.Length,
          &numBytesRead)) {
    auto err = GetLastError();
    return fmt::format(
        FMT_STRING("<ReadProcessMemory err:{}>"), win32ErrorToString(err));
  }

  return wideToMultibyteString<std::string>(cmd);
}

#endif // #ifdef _WIN32

ProcPidCmdLine getProcPidCmdLine(pid_t pid) {
  ProcPidCmdLine path;
  memcpy(path.data(), "/proc/", 6);
  auto digits =
      folly::to_ascii_decimal(path.data() + 6, path.data() + path.size(), pid);
  memcpy(path.data() + 6 + digits, "/cmdline", 9);
  return path;
}

struct StatusInfo {
  pid_t pid;
  pid_t ppid{};
  uid_t uid{};

  StatusInfo(pid_t pid, pid_t ppid, uid_t uid)
      : pid(pid), ppid(ppid), uid(uid) {}

  static std::optional<StatusInfo> create(pid_t pid) {
    pid_t ppid;
    uid_t uid;

    bool foundPpid{false};
    bool foundUid{false};

    std::string statusPath = folly::to<std::string>("/proc/", pid, "/status");
    std::string line;
    std::fstream fs(statusPath, std::ios_base::in);
    while (std::getline(fs, line)) {
      if (!foundUid) {
        foundUid = parseStatusLine(line, "Uid:", uid);
      }
      if (!foundPpid) {
        foundPpid = parseStatusLine(line, "PPid:", ppid);
      }
      if (foundUid && foundPpid) {
        return StatusInfo(pid, ppid, uid);
      }
    }
    XLOGF(DBG4, "Failed to read status for pid: {}", pid);
    return std::nullopt;
  }

  template <typename T>
  static bool
  parseStatusLine(std::string& line, std::string_view entry, T& val) {
    if (eden::starts_with(line, entry)) {
      std::istringstream iss(line.substr(entry.size()));
      iss >> val;
      return !iss.fail();
    }
    return false;
  }
};

} // namespace detail

namespace {

#ifdef __APPLE__
// This returns 256kb on my system
size_t queryKernArgMax() {
  int mib[2] = {CTL_KERN, KERN_ARGMAX};
  int argmax = 0;
  size_t size = sizeof(argmax);
  folly::checkUnixError(
      sysctl(mib, std::size(mib), &argmax, &size, nullptr, 0),
      "error retrieving KERN_ARGMAX via sysctl");
  XCHECK(argmax > 0) << "KERN_ARGMAX has a negative value!?";
  return size_t(argmax);
}

folly::StringPiece extractCommandLineFromProcArgs(
    const char* procargs,
    size_t len) {
  /* The format of procargs2 is:
     struct procargs2 {
        int argc;
        char [] executable image path;
        char [] null byte padding out to the word size;
        char [] argv0 with null terminator
        char [] argvN with null terminator
        char [] key=val of first env var (with null terminator)
        char [] key=val of second env var (with null terminator)
        ...
  */

  if (UNLIKELY(len < sizeof(int))) {
    // Should be impossible!
    return "<err:EUNDERFLOW>";
  }

  // Fetch the argc value for the target process
  int argCount = 0;
  memcpy(&argCount, procargs, sizeof(argCount));
  if (argCount < 1) {
    return "<err:BOGUS_ARGC>";
  }

  const char* end = procargs + len;
  // Skip over the image path
  const char* cmdline = procargs + sizeof(int);
  // look for NUL byte
  while (cmdline < end) {
    if (*cmdline == 0) {
      break;
    }
    ++cmdline;
  }
  // look for non-NUL byte
  while (cmdline < end) {
    if (*cmdline != 0) {
      break;
    }
    ++cmdline;
  }
  // now cmdline points to the start of the command line

  const char* ptr = cmdline;
  while (argCount > 0 && ptr < end) {
    if (*ptr == 0) {
      if (--argCount == 0) {
        return folly::StringPiece{cmdline, ptr};
      }
    }
    ptr++;
  }

  return folly::StringPiece{cmdline, end};
}

#endif

} // namespace

ProcessName readProcessName(pid_t pid) {
#ifdef __APPLE__
  // a Meyers Singleton to compute and cache this system parameter
  static size_t argMax = queryKernArgMax();

  std::vector<char> args;
  args.resize(argMax);

  char* procargs = args.data();
  size_t len = args.size();

  int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
  if (sysctl(mib, std::size(mib), procargs, &len, nullptr, 0) == -1) {
    // AFAICT, the sysctl will only fail in situations where the calling
    // process lacks privs to read the args from the target.
    // The errno value is a bland EINVAL in that case.
    // Regardless of the cause, we'd like to try to show something so we
    // fallback to using libproc to retrieve the image filename.

    // libproc is undocumented and unsupported, but the implementation is open
    // source:
    // https://opensource.apple.com/source/xnu/xnu-2782.40.9/libsyscall/wrappers/libproc/libproc.c
    // The return value is 0 on error, otherwise is the length of the buffer.
    // It takes care of overflow/truncation.

    // The buffer must be exactly PROC_PIDPATHINFO_MAXSIZE in size otherwise
    // an EOVERFLOW is generated (even if the buffer is larger!)
    args.resize(PROC_PIDPATHINFO_MAXSIZE);
    ssize_t rv = proc_pidpath(pid, args.data(), PROC_PIDPATHINFO_MAXSIZE);
    if (rv != 0) {
      return std::string{args.data(), args.data() + rv};
    }
    return folly::to<std::string>("<err:", errno, ">");
  }

  // The sysctl won't fail if the buffer is too small, but should set the len
  // value to approximately the used length on success.
  // If the buffer is too small it leaves
  // the value that was passed in as-is.  Therefore we can detect that our
  // buffer was too small if the size is >= the available data space.
  // The returned len in the success case seems to be smaller than the input
  // length.  For example, a successful call with len returned as 1012 requires
  // an input buffer of length 1029
  if (len >= args.size()) {
    return "<err:EOVERFLOW>";
  }

  return extractCommandLineFromProcArgs(procargs, len).str();
#elif _WIN32
  if (std::optional<std::string> cmd = detail::getProcessCommandLine(pid);
      cmd) {
    return std::move(*cmd);
  } else {
    ProcessHandle handle{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid)};
    if (!handle) {
      auto err = GetLastError();
      return fmt::format(FMT_STRING("<err:{}>"), win32ErrorToString(err));
    }

    // MAX_PATH on Windows is only 260 characters, but on recent Windows, this
    // constant doesn't represent the actual maximum length of a path, since
    // there is no exact value for it, and QueryFullProcessImageName doesn't
    // appear to be helpful in giving us the actual size of the path, we just
    // use a large enough value.
    wchar_t path[SHRT_MAX];
    DWORD size = SHRT_MAX;
    if (QueryFullProcessImageNameW(handle.get(), 0, path, &size) == 0) {
      auto err = GetLastError();
      return fmt::format(FMT_STRING("<err:{}>"), win32ErrorToString(err));
    }

    return wideToMultibyteString<std::string>(path);
  }
#else
  char target[1024];
  const auto fd = folly::openNoInt(
      detail::getProcPidCmdLine(pid).data(), O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    return folly::to<std::string>("<err:", errno, ">");
  }
  SCOPE_EXIT {
    folly::closeNoInt(fd);
  };

  ssize_t rv = folly::readFull(fd, target, sizeof(target));
  if (rv == -1) {
    return folly::to<std::string>("<err:", errno, ">");
  } else {
    // Could do something fancy if the entire buffer is filled, but it's better
    // if this code does as few syscalls as possible, so just truncate the
    // result.
    return std::string{target, target + rv};
  }
#endif
}

ProcessSimpleName readProcessSimpleName([[maybe_unused]] pid_t pid) {
#ifdef __APPLE__
  // Max length of process name returned from proc_name
  // https://opensource.apple.com/source/xnu/xnu-1228.0.2/bsd/sys/proc_info.h.auto.html
  std::vector<char> name;
  int32_t len = 2 * MAXCOMLEN + 1;
  name.resize(len);
  auto namePtr = name.data();

  auto ret = proc_name(pid, namePtr, len);
  if (ret > len) {
    // This should never happen.
    XLOGF(
        INFO,
        "proc_name for pid {} returned length greater than provided buffer.",
        pid);
  } else if (ret != 0) {
    name.resize(ret);
    return ProcessSimpleName(std::string(name.begin(), name.end()));
  } else {
    XLOGF(
        DBG2,
        "proc_name failed for pid {}: {} ({})",
        pid,
        folly::errnoStr(errno),
        errno);
  }
#endif
  return ProcessSimpleName("<unknown>");
}

/* static */
std::string ProcessUserInfo::uidToUsername(uid_t uid) {
// Convert UID to username
#ifdef _WIN32
  return "<unknown>";
#else
  auto userInfo = UserInfo::getPasswdUid(uid);
  return userInfo.pwd.pw_name;
#endif
}

std::optional<ProcessUserInfo> readUserInfo(
    pid_t pid,
    ReadUserInfoConfig config) {
#ifdef __APPLE__
  // Not implemented
  (void)pid;
  (void)config;
  return std::nullopt;
#elif _WIN32
  // Not implemented
  (void)pid;
  (void)config;
  return std::nullopt;
#else
  // Linux
  std::optional<ProcessUserInfo> userInfo = std::nullopt;
  std::optional<detail::StatusInfo> status;
  do {
    if (status.has_value()) {
      pid = status->ppid;
    }
    status = detail::StatusInfo::create(pid);
    if (!status.has_value()) {
      break;
    }

    if (!userInfo.has_value()) {
      userInfo = ProcessUserInfo{status->uid, status->uid};
    }
    userInfo->ruid = status->uid;

  } while (status->pid != 1 && status->uid == 0 && config.resolveRootUser);

  if (userInfo.has_value() && config.fetchUsernames) {
    userInfo->getRealUsername();
    userInfo->getEffectiveUsername();
  }

  return userInfo;
#endif
}

std::optional<pid_t> getParentProcessId([[maybe_unused]] pid_t pid) {
  std::optional<pid_t> ppid;
#ifdef __APPLE__
  // Future improvements might include caching of parent pid lookups. However,
  // as pids are recycled over time we would need some way to invalidate the
  // cache when necessary.
  proc_bsdinfo info;
  int32_t size = sizeof(info);
  auto ret = proc_pidinfo(
      pid,
      PROC_PIDTBSDINFO,
      true, // find zombies
      &info,
      size);

  if (ret == 0) {
    XLOGF(DBG3, "proc_pidinfo failed: {} ({})", folly::errnoStr(errno), errno);
  } else if (ret != size) {
    XLOGF(WARN, "proc_pidinfo failed returned an invalid size");
  } else if (info.pbi_ppid <= 0) {
    XLOGF(WARN, "proc_pidinfo returned an invalid parent pid.");
  } else {
    ppid.emplace(info.pbi_ppid);
  }
#endif

  return ppid;
}

} // namespace facebook::eden
