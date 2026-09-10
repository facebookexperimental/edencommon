/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/Unistd.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>

#include <eden/common/utils/Bug.h>
#include <folly/Exception.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Range.h>
#include <folly/logging/xlog.h>
#include <sigbus_memops.h>

#ifndef _WIN32
#include <fcntl.h>
#include <folly/portability/SysMman.h>
#include <sys/mman.h>
#endif

namespace facebook::eden {

struct MappedDiskVectorOptions {
  // The caller must install a signal handler that uses sigbus_try_handle().
  bool useSigbusProtection{false};
};

namespace detail {

/**
 * The precise value of kPageSize doesn't matter for correctness.  It's used
 * primarily as a microoptimization - MappedDiskVector attempts to avoid mapping
 * fractions of pages which lets it resize the file a bit less often.
 */
constexpr size_t kPageSize = 4096;

inline size_t roundUpToNonzeroPageSize(size_t s) {
  static_assert(
      0 == (kPageSize & (kPageSize - 1)), "kPageSize must be power of two");
  return std::max(kPageSize, (s + kPageSize - 1) & ~(kPageSize - 1));
}

/**
 * Enforce required properties of
 */
template <typename... T>
struct RecordTypeRequirements;

template <>
struct RecordTypeRequirements<> {
  using type = void;
};

template <typename T, typename... Rest>
struct RecordTypeRequirements<T, Rest...> {
  static_assert(
      std::is_standard_layout<T>::value,
      "Records must have standard layout");
  static_assert(
      std::is_trivially_destructible<T>::value,
      "MappedDiskVector does not support custom destructors");
  static_assert(
      std::is_trivially_move_assignable<T>::value,
      "Records will be relocated in memory");
  static_assert(
      std::is_trivially_copyable<T>::value,
      "Records must be safely copyable to and from mapped storage");
  static_assert(
      std::is_convertible<decltype(T::VERSION), uint32_t>::value,
      "Record's VERSION constant must convert to a uint32_t");
  static_assert(T::VERSION >= 0, "Record VERSION cannot be negative");
  static_assert(
      T::VERSION < std::numeric_limits<uint32_t>::max(),
      "Record VERSION must fit in 32 bits");

  using type = typename RecordTypeRequirements<Rest...>::type;
};

template <typename T, typename... OldVersions>
struct Migrator;
} // namespace detail

/**
 * MappedDiskVector is roughly analogous to std::vector, except it's backed by
 * a persistent memory-mapped file.
 *
 * MappedDiskVector is not thread-safe - the caller is
 * responsible for synchronization. It is safe for multiple threads to
 * simultaneously read, however.
 *
 * While alive, MappedDiskVector does acquire an exclusive flock on the
 * underlying fd to avoid multiple processes manipulating it at the same time.
 *
 * MappedDiskVector supports migrating from old formats to new formats via the
 * OldVersions template parameter. For any given type T, T::VERSION is written
 * into the header and used for version negotiation. sizeof(T) is also recorded
 * to prevent accidentally adding a field without changing the version.
 *
 * MappedDiskVector<A, B, C> will, if decoding the file as A fails, try to
 * decode as B and C, and if either succeeds, the file will be migrated to the
 * new format and reopened. In particular, each record will be constructed with
 * an instance of the type of the right. When migrating from C to A above,
 * the new file will contain values constructed with C{B{oldA}}.
 *
 * This type needs to be split into two: the non-template, untyped storage
 * class that manages resizing the file and mapping and parsing the header,
 * and the typed view that owns the storage and exposes it as a typed vector.
 *
 * The caller needs the ability to read userVersion and negotiate an upgrade.
 * I imagine a new type that owns the file handle and allows reading the file
 * size and header, independent of the desired T.  The call can use entrySize
 * and/or userVersion to decide whether to migrate the file's contents into
 * a new file.  Then this type would be moved into a new MappedDiskVector<T>
 * and accessed as a vector afterwards.
 */
template <
    typename T,
    typename = typename detail::RecordTypeRequirements<T>::type>
class MappedDiskVector {
 public:
  /**
   * Opens or creates the MappedDiskVector at the specified path.  The path is
   * only used to open the file - a single file descriptor is used from then on
   * with the underlying inode resized in place.
   *
   * If the load fails because of a version mismatch, the types specified in
   * OldVersions are tried sequentially. If one succeeds, the entries are
   * converted one-by-one into the new format and the new table replaces the
   * old.
   */
  template <typename... OldVersions>
  static MappedDiskVector open(
      folly::StringPiece path,
      std::function<void()> afterMmap = nullptr,
      MappedDiskVectorOptions options = {}) {
    folly::File file{path, O_RDWR | O_CREAT | O_CLOEXEC, 0600};

    if (!file.try_lock()) {
      folly::throwSystemError("failed to acquire lock on ", path);
    }

    struct stat st;
    folly::checkUnixError(
        fstat(file.fd(), &st), "fstat failed on MappedDiskVector path ", path);

    if (st.st_size == 0) {
      return initializeFromScratch(std::move(file), options);
    }

    Header header;
    ssize_t readBytes =
        folly::preadNoInt(file.fd(), &header, sizeof(header), 0);
    if (readBytes == -1) {
      folly::throwSystemError("failed to read MappedDiskVector header");
    } else if (readBytes != sizeof(header)) {
      XLOGF(
          WARNING,
          "file contains incomplete header: only read {} bytes",
          readBytes);
      throw std::runtime_error("Incomplete MappedDiskVector header");
    }

    if (kMagic != header.magic || header.version != 1 ||
        static_cast<ssize_t>(sizeof(header)) > st.st_size ||
        header.recordSize == 0 ||
        // careful not to overflow by multiplying entryCount by recordSize
        header.entryCount > (st.st_size - sizeof(header)) / header.recordSize ||
        header.unused != 0) {
      throw std::runtime_error(
          "Invalid header: this is probably not a MappedDiskVector file");
    }

    // Verify that every given record type has a unique VERSION value.
    // This check could be done at compile time.
    static constexpr std::array<uint32_t, 1 + sizeof...(OldVersions)> versions =
        {T::VERSION, OldVersions::VERSION...};

    for (size_t i = 0; i < versions.size(); ++i) {
      for (size_t j = i + 1; j < versions.size(); ++j) {
        if (versions[i] == versions[j]) {
          throw std::logic_error(
              folly::to<std::string>(
                  "Duplicate VERSION detected in record types: ", versions[i]));
        }
      }
    }

    // Does this file match the primary record type? If so, we're done.
    if (T::VERSION == header.recordVersion) {
      if (sizeof(T) != header.recordSize) {
        throw std::runtime_error(
            folly::to<std::string>(
                "Record size does not match size recorded in file. Expected ",
                sizeof(T),
                " but file has ",
                header.recordSize));
      }
      return MappedDiskVector{
          std::move(file),
          st.st_size,
          header.entryCount,
          std::move(afterMmap),
          options};
    }

    // Try to migrate from an old record format if any match.
    static constexpr std::array<size_t, sizeof...(OldVersions)> sizes = {
        sizeof(OldVersions)...};
    for (size_t i = 0; i < sizes.size(); ++i) {
      if (versions[i + 1] == header.recordVersion) {
        if (sizes[i] != header.recordSize) {
          throw std::runtime_error(
              folly::to<std::string>(
                  "Record version matches old record type but record size differs. ",
                  "Expected ",
                  sizes[i],
                  " but file has ",
                  header.recordSize));
        }
        return detail::Migrator<T, OldVersions...>::migrateFrom(
            path,
            std::move(file),
            st.st_size,
            header.entryCount,
            i,
            options,
            [](const auto& from) { return T{from}; });
      }
    }

    throw std::runtime_error(
        folly::to<std::string>(
            "Unexpected record size and version. "
            "Expected size=",
            sizeof(T),
            ", version=",
            T::VERSION,
            " but got size=",
            header.recordSize,
            ", version=",
            header.recordVersion));
  }

  template <typename... OldVersions>
  static MappedDiskVector open(
      folly::StringPiece path,
      MappedDiskVectorOptions options) {
    return open<OldVersions...>(path, nullptr, options);
  }

  /**
   * Creates a new MappedDiskVector at the specified path, overwriting any that
   * was there prior.
   */
  static MappedDiskVector createOrOverwrite(
      folly::StringPiece path,
      MappedDiskVectorOptions options = {}) {
    folly::File file{
        path, O_RDWR | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600};
    if (!file.try_lock()) {
      folly::throwSystemError("failed to acquire lock on ", path);
    }

    return initializeFromScratch(std::move(file), options);
  }

  explicit MappedDiskVector() = delete;
  MappedDiskVector(const MappedDiskVector&) = delete;
  MappedDiskVector& operator=(const MappedDiskVector&) = delete;

  MappedDiskVector(MappedDiskVector&& other) noexcept
      : file_(std::move(other.file_)),
        useSigbusProtection_(other.useSigbusProtection_) {
    begin_ = other.begin_;
    end_ = other.end_;
    map_ = other.map_;
    mapSizeInBytes_ = other.mapSizeInBytes_;

    other.begin_ = nullptr;
    other.end_ = nullptr;
    other.map_ = nullptr;
    other.mapSizeInBytes_ = 0;
  }

  MappedDiskVector& operator=(MappedDiskVector&& other) noexcept {
    if (map_) {
      munmap(map_, mapSizeInBytes_);
    }

    file_ = std::move(other.file_);
    begin_ = other.begin_;
    end_ = other.end_;
    map_ = other.map_;
    mapSizeInBytes_ = other.mapSizeInBytes_;
    useSigbusProtection_ = other.useSigbusProtection_;

    other.begin_ = nullptr;
    other.end_ = nullptr;
    other.map_ = nullptr;
    other.mapSizeInBytes_ = 0;

    return *this;
  }

  ~MappedDiskVector() {
    if (map_) {
      munmap(map_, mapSizeInBytes_);
    }
  }

  size_t size() const {
    return end_ - begin_;
  }

  size_t capacity() const {
    // round down
    return (mapSizeInBytes_ - sizeof(Header)) / sizeof(T);
  }

  T get(size_t index) const {
    XCHECK_LT(index, size());
    std::array<std::byte, sizeof(T)> bytes;
    copyFromMapped(bytes.data(), begin_ + index, sizeof(T));
    return std::bit_cast<T>(bytes);
  }

  void set(size_t index, const T& value) {
    XCHECK_LT(index, size());
    copyToMapped(begin_ + index, &value, sizeof(T));
  }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    T value{std::forward<Args>(args)...};

    if (!hasRoom(1)) {
      static_assert(
          sizeof(GROWTH_IN_PAGES) * detail::kPageSize >= sizeof(T),
          "Growth must expand the file more than a single record");

      size_t oldSize = size();
      size_t newFileSize =
          mapSizeInBytes_ + GROWTH_IN_PAGES * detail::kPageSize;

      // Always keep the file size a whole number of pages.
      XCHECK_EQ(0ul, newFileSize % detail::kPageSize);

      extendFile(file_.fd(), newFileSize);

#ifdef __APPLE__
      auto newMap = mmap(
          nullptr,
          newFileSize,
          PROT_READ | PROT_WRITE,
          MAP_SHARED,
          file_.fd(),
          0);
#else
      auto newMap = mremap(map_, mapSizeInBytes_, newFileSize, MREMAP_MAYMOVE);
#endif
      if (newMap == MAP_FAILED) {
        folly::throwSystemError(
            folly::to<std::string>(
                "mremap failed when growing capacity from ",
                mapSizeInBytes_,
                " to ",
                newFileSize));
      }

#ifdef __APPLE__
      munmap(map_, mapSizeInBytes_);
#endif
      map_ = newMap;
      mapSizeInBytes_ = newFileSize;

      begin_ = reinterpret_cast<T*>(static_cast<Header*>(newMap) + 1);
      end_ = begin_ + oldSize;

      // Pre-fault the newly grown region. map_ is page-aligned (mmap/mremap
      // guarantee) and the old mapping size is a multiple of GROWTH_IN_PAGES *
      // kPageSize, so the address is system-page-aligned on all platforms.
      populateForWrite(
          static_cast<char*>(map_) +
              (mapSizeInBytes_ - GROWTH_IN_PAGES * detail::kPageSize),
          GROWTH_IN_PAGES * detail::kPageSize);
    }

    T* out = end_;
    copyToMapped(out, &value, sizeof(T));
    writeEntryCount(size() + 1);
    end_ = out + 1;
  }

  void pop_back() {
    XDCHECK_GT(end_, begin_);
    writeEntryCount(size() - 1);
    --end_;
  }

 private:
  static constexpr uint32_t kMagic = 0x0056444d; // "MDV\0"

  struct Header {
    uint32_t magic;
    uint32_t version; // 1
    uint32_t recordVersion; // T::VERSION
    uint32_t recordSize; // sizeof(T)
    uint64_t entryCount; // end() - begin()
    uint64_t unused; // for alignment
  };
  static_assert(
      32 == sizeof(Header),
      "changing the header size would invalidate all files");
  static_assert(
      0 == sizeof(Header) % 16,
      "header alignment is 16 bytes in case someone uses SSE values");

  static constexpr size_t GROWTH_IN_PAGES = 256;

  void copyFromMapped(void* destination, const void* source, size_t length)
      const {
    if (useSigbusProtection_) {
      if (!sigbus_try_memcpy(destination, source, length)) {
        throw std::runtime_error("failed to read MappedDiskVector entry");
      }
      return;
    }
    std::memcpy(destination, source, length);
  }

  void copyToMapped(void* destination, const void* source, size_t length)
      const {
    if (useSigbusProtection_) {
      if (!sigbus_try_memcpy(destination, source, length)) {
        throw std::runtime_error("failed to write MappedDiskVector entry");
      }
      return;
    }
    populateForWrite(destination, length);
    std::memcpy(destination, source, length);
  }

  void writeEntryCount(size_t count) {
    const uint64_t entryCount = count;
    const auto written = folly::pwriteNoInt(
        file_.fd(),
        &entryCount,
        sizeof(entryCount),
        offsetof(Header, entryCount));
    if (written == -1) {
      folly::throwSystemError("failed to update MappedDiskVector entry count");
    }
    if (written != sizeof(entryCount)) {
      throw std::runtime_error(
          "failed to write complete MappedDiskVector entry count");
    }
  }

  // Pre-fault pages with write intent to detect disk-full errors as exceptions
  // instead of SIGBUS. Even when fallocate succeeds, pages may be unwritable:
  // btrfs can exhaust data space while metadata space remains, thin-provisioned
  // storage can overcommit, and network filesystems can fail between allocation
  // and write. MADV_POPULATE_WRITE forces the kernel to back each page now.
  // Requires Linux 5.14+; EINVAL means unsupported.
  static void populateForWrite(const void* addr, size_t length) {
#if defined(__linux__) && defined(MADV_POPULATE_WRITE)
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto end = start + length;
    XCHECK_GE(end, start);

    // madvise() requires a page-aligned address, but entry writes may target a
    // record in the middle of the mapping. Populate the pages covering it.
    const auto pageSize = systemPageSize();
    auto alignedStartAddress = start - (start % pageSize);
    auto alignedEndAddress = ((end + pageSize - 1) / pageSize) * pageSize;
    auto* alignedStart =
        static_cast<const char*>(addr) - (start - alignedStartAddress);
    if (madvise(
            const_cast<char*>(alignedStart),
            alignedEndAddress - alignedStartAddress,
            MADV_POPULATE_WRITE) != 0 &&
        errno != EINVAL) {
      folly::throwSystemError(
          "failed to populate MappedDiskVector pages (disk full?)");
    }
#else
    (void)addr;
    (void)length;
#endif
  }

  static size_t systemPageSize() {
    static const size_t pageSize = [] {
      auto value = sysconf(_SC_PAGESIZE);
      return value > 0 ? static_cast<size_t>(value) : detail::kPageSize;
    }();
    return pageSize;
  }

  // Extend a file to newSize bytes, pre-allocating disk blocks where
  // supported. Using fallocate detects ENOSPC early with a clean error
  // instead of creating a sparse file that causes SIGBUS on write.
  static void extendFile(int fd, off_t newSize) {
#ifdef __linux__
    if (fallocate(fd, 0, 0, newSize) != 0) {
      if (errno == EOPNOTSUPP) {
        // Filesystem doesn't support fallocate, fall back to ftruncate.
        if (folly::ftruncateNoInt(fd, newSize) != 0) {
          folly::throwSystemError("failed to extend file");
        }
      } else {
        folly::throwSystemError("fallocate failed when extending file");
      }
    }
#else
    if (folly::ftruncateNoInt(fd, newSize) != 0) {
      folly::throwSystemError("failed to extend file");
    }
#endif
  }

  static MappedDiskVector initializeFromScratch(
      folly::File file,
      MappedDiskVectorOptions options) {
    // Start the file large enough to handle the header and a little under one
    // round one of growth.
    constexpr size_t initialSize = GROWTH_IN_PAGES * detail::kPageSize;
    static_assert(
        initialSize >= sizeof(Header) + sizeof(T),
        "Initial size must include enough space for the header and at least one element.");
    extendFile(file.fd(), initialSize);

    Header header;
    header.magic = kMagic;
    header.version = 1;
    header.recordVersion = T::VERSION;
    header.recordSize = sizeof(T);
    header.entryCount = 0;
    header.unused = 0;
    ssize_t written = folly::pwriteNoInt(file.fd(), &header, sizeof(header), 0);
    if (-1 == written) {
      folly::throwSystemError("Failed to write initial header");
    }
    if (written != sizeof(header)) {
      throw std::runtime_error("Failed to write complete initial header");
    }

    return MappedDiskVector{
        std::move(file), initialSize, header.entryCount, nullptr, options};
  }

  explicit MappedDiskVector(
      folly::File file,
      off_t fileSize,
      size_t currentEntryCount,
      const std::function<void()>& afterMmap = nullptr,
      MappedDiskVectorOptions options = {})
      : file_(std::move(file)),
        useSigbusProtection_(
            options.useSigbusProtection && sigbus_is_protected()) {
    // It's worth keeping the file and mapping a whole number of pages to
    // avoid wasting an partial page at the end.  Note that this is an
    // optimization and it doesn't matter if kPageSize differs from the
    // system page size.
    size_t desiredSize = detail::roundUpToNonzeroPageSize(fileSize);
    if (fileSize != static_cast<ssize_t>(desiredSize)) {
      if (fileSize) {
        XLOGF(
            WARNING,
            "Warning: MappedDiskVector file size not multiple of page size: {}",
            fileSize);
      }
      extendFile(file_.fd(), desiredSize);
    }

    // Call readahead() here?  Offer it as optional functionality?
    // InodeTable needs to traverse every record immediately after opening.

    auto map = mmap(
        nullptr,
        desiredSize,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        file_.fd(),
        0);
    if (map == MAP_FAILED) {
      folly::throwSystemError("mmap failed on file open");
    }

    if (afterMmap) {
      afterMmap();
    }

    try {
      populateForWrite(map, desiredSize);
    } catch (...) {
      munmap(map, desiredSize);
      throw;
    }

    // Throw no exceptions between assigning the fields.

    map_ = map;
    mapSizeInBytes_ = desiredSize;
    static_assert(
        alignof(Header) >= alignof(T),
        "T must not have stricter alignment requirements than Header");
    begin_ = reinterpret_cast<T*>(static_cast<Header*>(map) + 1);
    end_ = begin_ + currentEntryCount;

    // Just double-check that the accessed region is within the map.
    XCHECK_LE(
        reinterpret_cast<char*>(end_),
        static_cast<char*>(map_) + mapSizeInBytes_);
  }

  bool hasRoom(size_t amount) const {
    // Technically, the expression (end_ + amount) is constructing a pointer
    // past the end of the "object" (mmap) and is thus UB.  But hopefully no
    // compiler can see that.
    return reinterpret_cast<char*>(end_ + amount) <=
        static_cast<char*>(map_) + mapSizeInBytes_;
  }

  // these two should be at the front of the struct
  T* begin_{nullptr};
  T* end_{nullptr};

  void* map_{nullptr};
  size_t mapSizeInBytes_{0}; // must be nonzero, multiple of page size

  folly::File file_;
  bool useSigbusProtection_{false};

  template <typename T_, typename... OldVersions>
  friend struct detail::Migrator;
};

namespace detail {
template <typename T>
struct Migrator<T> {
  template <typename ConvertFn>
  [[noreturn]] static MappedDiskVector<T> migrateFrom(
      folly::StringPiece /*path*/,
      folly::File /*file*/,
      off_t /*fileSize*/,
      size_t /*currentEntryCount*/,
      size_t /*oldVersionIndex*/,
      MappedDiskVectorOptions /*options*/,
      ConvertFn /*convert*/) {
    EDEN_BUG() << "oldVersionIndex >= sizeof...(OldVersions)";
  }
};

template <typename T, typename First, typename... Rest>
struct Migrator<T, First, Rest...> {
  template <typename ConvertFn>
  static MappedDiskVector<T> migrateFrom(
      folly::StringPiece path,
      folly::File file,
      off_t fileSize,
      size_t currentEntryCount,
      size_t oldVersionIndex,
      MappedDiskVectorOptions options,
      ConvertFn convert) {
    using namespace folly::literals;

    // static assert type of fn() is First -> T

    if (oldVersionIndex == 0) {
      // At this point, it's clear the original file is compatible with First.
      // Load it, migrate each element to a new temporary file, and move the
      // temporary file over the original.
      MappedDiskVector<First> original{
          std::move(file), fileSize, currentEntryCount, nullptr, options};

      auto tmpPath = folly::to<std::string>(path, ".tmp");
      auto newVector = MappedDiskVector<T>::createOrOverwrite(tmpPath, options);
      try {
        // TODO: newVector.reserve
        for (size_t i = 0; i < original.size(); ++i) {
          newVector.emplace_back(convert(original.get(i)));
        }

        if (rename(tmpPath.c_str(), path.str().c_str())) {
          folly::throwSystemError(
              "rename() failed while migrating MDV formats");
        }

        return newVector;
      } catch (const std::exception&) {
        unlink(tmpPath.c_str());
        throw;
      }
    }

    return Migrator<T, Rest...>::migrateFrom(
        path,
        std::move(file),
        fileSize,
        currentEntryCount,
        oldVersionIndex - 1,
        options,
        [=](const auto& from) { return convert(First{from}); });
  }
};

} // namespace detail

} // namespace facebook::eden
