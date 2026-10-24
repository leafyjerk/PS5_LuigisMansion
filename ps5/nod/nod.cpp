/*
 * Dusklight for the PlayStation 5 - the part of nod's C API Aurora and Borealis
 * call, for a GameCube disc image as .iso or .ciso.
 *
 * nod (https://github.com/encounter/nod, include/nod.h is its header at the
 * release Aurora pins) is a Rust library that reads every GameCube and Wii
 * image format. There is no Rust toolchain for the console, and a GameCube
 * .iso needs none of it: the disc is unencrypted, with a boot header, an
 * apploader, a DOL and a file system table at fixed places. This implements
 * the functions the title links, with nod's behaviour, for that one format.
 * CISO, which only leaves out the disc's empty blocks, is read as well. Compressed
 * images (RVZ, GCZ, WIA, WBFS) and Wii discs are refused with a message.
 *
 * SPDX-License-Identifier: MIT
 */
#include "nod.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

thread_local std::string t_error;

NodResult fail(NodResult result, const char* format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  t_error = message;
  return result;
}

uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

constexpr size_t kBootSize = 0x440;
constexpr size_t kBi2Size = 0x2000;
constexpr uint64_t kApploaderOffset = kBootSize + kBi2Size;
constexpr size_t kMaxSystemBlob = 64u << 20;

constexpr uint64_t kGameCubeDiscSize = 1459978240;
constexpr size_t kCisoHeaderSize = 0x8000;
constexpr size_t kCisoMapSize = kCisoHeaderSize - 8;

// The image, shared by every handle opened from it
struct Disc {
  NodDiscStream stream{};
  uint64_t fileSize = 0;  // the stream's
  uint64_t size = 0;      // the disc's
  std::mutex lock;  // the stream's callbacks are not promised to be reentrant
  // CISO: the disc in blocks, each either stored (in order, after the header) or all zero
  uint32_t cisoBlockSize = 0;
  std::vector<int32_t> cisoBlocks;  // a block's place among the stored ones, or -1

  ~Disc() {
    if (stream.close != nullptr) {
      stream.close(stream.user_data);
    }
  }

  // The stream itself; the lock is held
  int64_t readFile(uint64_t offset, uint8_t* out, size_t len) {
    size_t done = 0;
    while (done < len) {
      const int64_t n = stream.read_at(stream.user_data, offset + done, out + done, len - done);
      if (n < 0) {
        return -1;
      }
      if (n == 0) {
        break;
      }
      done += static_cast<size_t>(n);
    }
    return static_cast<int64_t>(done);
  }

  // The disc
  int64_t readAt(uint64_t offset, void* out, size_t len) {
    if (offset >= size) {
      return 0;
    }
    len = static_cast<size_t>(std::min<uint64_t>(len, size - offset));
    auto* p = static_cast<uint8_t*>(out);
    std::lock_guard guard{lock};
    if (cisoBlockSize == 0) {
      return readFile(offset, p, len);
    }
    size_t done = 0;
    while (done < len) {
      const uint64_t at = offset + done;
      const uint64_t block = at / cisoBlockSize;
      const uint64_t within = at % cisoBlockSize;
      const size_t part = static_cast<size_t>(std::min<uint64_t>(len - done, cisoBlockSize - within));
      const int32_t stored = block < cisoBlocks.size() ? cisoBlocks[block] : -1;
      if (stored < 0) {
        memset(p + done, 0, part);
      } else {
        const int64_t n =
            readFile(kCisoHeaderSize + uint64_t(stored) * cisoBlockSize + within, p + done, part);
        if (n < 0) {
          return -1;
        }
        if (static_cast<size_t>(n) < part) {
          memset(p + done + n, 0, part - static_cast<size_t>(n));  // a stored block cut short
        }
      }
      done += part;
    }
    return static_cast<int64_t>(done);
  }

  bool readExact(uint64_t offset, void* out, size_t len) {
    return readAt(offset, out, len) == static_cast<int64_t>(len);
  }
};

struct Partition {
  std::vector<uint8_t> boot, bi2, apploader, dol, fst;
  uint32_t entries = 0;      // FST entries, the root included
  const char* names = nullptr;  // the FST's string table
  size_t namesSize = 0;
};

}  // namespace

struct NodHandle {
  enum class Kind { Disc, Partition, File } kind = Kind::Disc;
  std::shared_ptr<Disc> disc;
  std::shared_ptr<Partition> partition;
  // Disc and File: a window of the image, and the position in it
  uint64_t start = 0;
  uint64_t length = 0;
  uint64_t position = 0;
};

namespace {

bool readBlob(Disc& disc, uint64_t offset, uint64_t size, std::vector<uint8_t>& out, const char* what) {
  if (size > kMaxSystemBlob || offset + size > disc.size) {
    fail(NOD_RESULT_ERR_FORMAT, "%s is outside the disc image (offset 0x%llx, size 0x%llx)", what,
         static_cast<unsigned long long>(offset), static_cast<unsigned long long>(size));
    return false;
  }
  out.resize(static_cast<size_t>(size));
  if (!disc.readExact(offset, out.data(), out.size())) {
    fail(NOD_RESULT_ERR_IO, "failed to read %s", what);
    return false;
  }
  return true;
}

std::shared_ptr<Partition> loadPartition(Disc& disc) {
  auto partition = std::make_shared<Partition>();
  if (!readBlob(disc, 0, kBootSize, partition->boot, "the boot header") ||
      !readBlob(disc, kBootSize, kBi2Size, partition->bi2, "bi2")) {
    return nullptr;
  }
  const uint8_t* boot = partition->boot.data();
  const uint32_t dolOffset = be32(boot + 0x420);
  const uint32_t fstOffset = be32(boot + 0x424);
  const uint32_t fstSize = be32(boot + 0x428);

  // The apploader: a 0x20 byte header, then its code and its trailer
  uint8_t apploaderHeader[0x20];
  if (!disc.readExact(kApploaderOffset, apploaderHeader, sizeof(apploaderHeader))) {
    fail(NOD_RESULT_ERR_IO, "failed to read the apploader header");
    return nullptr;
  }
  const uint64_t apploaderSize =
      0x20 + uint64_t(be32(apploaderHeader + 0x14)) + uint64_t(be32(apploaderHeader + 0x18));
  if (!readBlob(disc, kApploaderOffset, apploaderSize, partition->apploader, "the apploader")) {
    return nullptr;
  }

  // The DOL: 7 text and 11 data sections, an offset and a size each
  uint8_t dolHeader[0x100];
  if (!disc.readExact(dolOffset, dolHeader, sizeof(dolHeader))) {
    fail(NOD_RESULT_ERR_IO, "failed to read the DOL header");
    return nullptr;
  }
  uint64_t dolSize = sizeof(dolHeader);
  for (int section = 0; section < 18; section++) {
    const uint64_t offset = be32(dolHeader + section * 4);
    const uint64_t size = be32(dolHeader + 0x90 + section * 4);
    if (size != 0) {
      dolSize = std::max(dolSize, offset + size);
    }
  }
  if (!readBlob(disc, dolOffset, dolSize, partition->dol, "the DOL") ||
      !readBlob(disc, fstOffset, fstSize, partition->fst, "the file system table")) {
    return nullptr;
  }

  if (partition->fst.size() < 12) {
    fail(NOD_RESULT_ERR_FORMAT, "the file system table is empty");
    return nullptr;
  }
  partition->entries = be32(partition->fst.data() + 8);
  if (partition->entries == 0 || uint64_t(partition->entries) * 12 > partition->fst.size()) {
    fail(NOD_RESULT_ERR_FORMAT, "the file system table has %u entries in %zu bytes", partition->entries,
         partition->fst.size());
    return nullptr;
  }
  partition->names = reinterpret_cast<const char*>(partition->fst.data()) + size_t(partition->entries) * 12;
  partition->namesSize = partition->fst.size() - size_t(partition->entries) * 12;
  // every name must end inside the table
  if (partition->namesSize == 0 || partition->names[partition->namesSize - 1] != '\0') {
    partition->fst.push_back(0);
    partition->names = reinterpret_cast<const char*>(partition->fst.data()) + size_t(partition->entries) * 12;
    partition->namesSize++;
  }
  return partition;
}

struct FstEntry {
  bool directory;
  const char* name;
  uint32_t offset;  // a file's place in the image; a directory's parent
  uint32_t size;    // a file's length; a directory's end index
};

bool fstEntry(const Partition& partition, uint32_t index, FstEntry& out) {
  if (index >= partition.entries) {
    return false;
  }
  const uint8_t* raw = partition.fst.data() + size_t(index) * 12;
  const uint32_t nameOffset = be32(raw) & 0x00ffffff;
  out.directory = raw[0] != 0;
  out.name = nameOffset < partition.namesSize ? partition.names + nameOffset : "";
  out.offset = be32(raw + 4);
  out.size = be32(raw + 8);
  return true;
}

bool sameName(const char* a, const char* b, size_t bLength) {
  size_t i = 0;
  for (; i < bLength; i++) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
    if (a[i] == '\0' || lower(a[i]) != lower(b[i])) {
      return false;
    }
  }
  return a[i] == '\0';
}

}  // namespace

extern "C" {

const char* nod_error_message(void) { return t_error.empty() ? nullptr : t_error.c_str(); }

NodResult nod_disc_open_stream(const NodDiscStream* stream, const NodDiscOptions* options, NodHandle** out) {
  (void)options;
  if (out == nullptr) {
    return fail(NOD_RESULT_ERR_OTHER, "no output handle");
  }
  *out = nullptr;
  if (stream == nullptr || stream->read_at == nullptr || stream->stream_len == nullptr) {
    return fail(NOD_RESULT_ERR_OTHER, "the disc stream has no read or length callback");
  }
  auto disc = std::make_shared<Disc>();
  disc->stream = *stream;  // owned from here: closed with the last handle
  const int64_t length = stream->stream_len(stream->user_data);
  if (length < 0) {
    return fail(NOD_RESULT_ERR_IO, "failed to get the disc image's size");
  }
  disc->fileSize = disc->size = static_cast<uint64_t>(length);

  // CISO: "CISO", the block size, then a byte a block that says whether it is stored
  {
    std::vector<uint8_t> ciso(kCisoHeaderSize);
    if (disc->fileSize >= kCisoHeaderSize && disc->readExact(0, ciso.data(), 8) && memcmp(ciso.data(), "CISO", 4) == 0) {
      if (!disc->readExact(0, ciso.data(), ciso.size())) {
        return fail(NOD_RESULT_ERR_IO, "failed to read the CISO header");
      }
      const uint32_t blockSize = uint32_t(ciso[4]) | (uint32_t(ciso[5]) << 8) | (uint32_t(ciso[6]) << 16) |
                                 (uint32_t(ciso[7]) << 24);
      if (blockSize < 0x8000 || (blockSize & (blockSize - 1)) != 0) {
        return fail(NOD_RESULT_ERR_FORMAT, "the CISO image has a block size of %u", blockSize);
      }
      std::vector<int32_t> blocks(kCisoMapSize, -1);
      int32_t stored = 0;
      size_t used = 0;
      for (size_t block = 0; block < kCisoMapSize; block++) {
        if (ciso[8 + block] != 0) {
          blocks[block] = stored++;
          used = block + 1;
        }
      }
      if (used == 0) {
        return fail(NOD_RESULT_ERR_FORMAT, "the CISO image stores no block");
      }
      blocks.resize(used);
      disc->cisoBlocks = std::move(blocks);
      disc->cisoBlockSize = blockSize;
      // A GameCube disc's size is not a whole number of blocks: the last one is cut to it
      disc->size = uint64_t(used) * blockSize;
      if (disc->size > kGameCubeDiscSize && disc->size - kGameCubeDiscSize < blockSize) {
        disc->size = kGameCubeDiscSize;
      }
    }
  }

  uint8_t header[0x20];
  if (!disc->readExact(0, header, sizeof(header))) {
    return fail(NOD_RESULT_ERR_FORMAT, "the file is too small to be a disc image");
  }
  if (!NOD_MAGIC_EQ(header + 0x1c, GCN_MAGIC)) {
    if (NOD_MAGIC_EQ(header + 0x18, WII_MAGIC)) {
      return fail(NOD_RESULT_ERR_FORMAT,
                  "Wii disc images are not supported on PlayStation 5: use the GameCube version");
    }
    return fail(NOD_RESULT_ERR_FORMAT,
                "not a GameCube .iso or .ciso disc image: convert the dump first (RVZ, GCZ, WIA and WBFS "
                "are not supported on PlayStation 5)");
  }

  auto* handle = new NodHandle;
  handle->kind = NodHandle::Kind::Disc;
  handle->length = disc->size;
  handle->disc = std::move(disc);
  *out = handle;
  return NOD_RESULT_OK;
}

NodResult nod_disc_open(const char* path, const NodDiscOptions* options, NodHandle** out) {
  (void)options;
  if (out != nullptr) {
    *out = nullptr;
  }
  if (path == nullptr || out == nullptr) {
    return fail(NOD_RESULT_ERR_OTHER, "no path or output handle");
  }
  FILE* file = fopen(path, "rb");
  if (file == nullptr) {
    return fail(NOD_RESULT_ERR_IO, "failed to open %s", path);
  }
  const NodDiscStream stream{
      .user_data = file,
      .read_at = [](void* user, uint64_t offset, void* buffer, size_t len) -> int64_t {
        auto* f = static_cast<FILE*>(user);
        if (fseeko(f, static_cast<off_t>(offset), SEEK_SET) != 0) {
          return -1;
        }
        const size_t n = fread(buffer, 1, len, f);
        return n == 0 && ferror(f) ? -1 : static_cast<int64_t>(n);
      },
      .stream_len = [](void* user) -> int64_t {
        auto* f = static_cast<FILE*>(user);
        if (fseeko(f, 0, SEEK_END) != 0) {
          return -1;
        }
        return static_cast<int64_t>(ftello(f));
      },
      .close = [](void* user) { fclose(static_cast<FILE*>(user)); },
  };
  return nod_disc_open_stream(&stream, options, out);
}

NodResult nod_disc_open_partition_kind(NodHandle* disc, uint32_t kind, const NodPartitionOptions* options,
                                       NodHandle** out) {
  (void)options;
  if (out == nullptr) {
    return fail(NOD_RESULT_ERR_OTHER, "no output handle");
  }
  *out = nullptr;
  if (disc == nullptr || disc->kind != NodHandle::Kind::Disc) {
    return fail(NOD_RESULT_ERR_INVALID_HANDLE, "not a disc handle");
  }
  if (kind != NOD_PARTITION_KIND_DATA) {
    return fail(NOD_RESULT_ERR_NOT_FOUND, "a GameCube disc has a data partition only");
  }
  auto partition = loadPartition(*disc->disc);
  if (!partition) {
    return NOD_RESULT_ERR_FORMAT;
  }
  auto* handle = new NodHandle;
  handle->kind = NodHandle::Kind::Partition;
  handle->disc = disc->disc;
  handle->partition = std::move(partition);
  *out = handle;
  return NOD_RESULT_OK;
}

NodResult nod_disc_open_partition(NodHandle* disc, uint32_t index, const NodPartitionOptions* options,
                                  NodHandle** out) {
  if (index != 0) {
    if (out != nullptr) {
      *out = nullptr;
    }
    return fail(NOD_RESULT_ERR_NOT_FOUND, "a GameCube disc has one partition");
  }
  return nod_disc_open_partition_kind(disc, NOD_PARTITION_KIND_DATA, options, out);
}

NodResult nod_partition_open_file(NodHandle* partition, uint32_t fst_index, NodHandle** out) {
  if (out == nullptr) {
    return fail(NOD_RESULT_ERR_OTHER, "no output handle");
  }
  *out = nullptr;
  if (partition == nullptr || partition->kind != NodHandle::Kind::Partition) {
    return fail(NOD_RESULT_ERR_INVALID_HANDLE, "not a partition handle");
  }
  FstEntry entry;
  if (!fstEntry(*partition->partition, fst_index, entry) || entry.directory) {
    return fail(NOD_RESULT_ERR_NOT_FOUND, "no file at index %u of the file system table", fst_index);
  }
  auto* handle = new NodHandle;
  handle->kind = NodHandle::Kind::File;
  handle->disc = partition->disc;
  handle->start = entry.offset;
  handle->length = entry.size;
  *out = handle;
  return NOD_RESULT_OK;
}

void nod_free(NodHandle* handle) { delete handle; }

int64_t nod_read(NodHandle* handle, uint8_t* buf, size_t len) {
  if (handle == nullptr || handle->kind == NodHandle::Kind::Partition) {
    fail(NOD_RESULT_ERR_INVALID_HANDLE, "not a readable handle");
    return -1;
  }
  if (handle->position >= handle->length) {
    return 0;
  }
  len = static_cast<size_t>(std::min<uint64_t>(len, handle->length - handle->position));
  const int64_t n = handle->disc->readAt(handle->start + handle->position, buf, len);
  if (n < 0) {
    fail(NOD_RESULT_ERR_IO, "failed to read the disc image");
    return -1;
  }
  handle->position += static_cast<uint64_t>(n);
  return n;
}

int64_t nod_seek(NodHandle* handle, int64_t offset, int32_t whence) {
  if (handle == nullptr || handle->kind == NodHandle::Kind::Partition) {
    fail(NOD_RESULT_ERR_INVALID_HANDLE, "not a seekable handle");
    return -1;
  }
  int64_t base;
  switch (whence) {
  case 0:
    base = 0;
    break;
  case 1:
    base = static_cast<int64_t>(handle->position);
    break;
  case 2:
    base = static_cast<int64_t>(handle->length);
    break;
  default:
    fail(NOD_RESULT_ERR_OTHER, "seek from an unknown origin %d", whence);
    return -1;
  }
  if (base + offset < 0) {
    fail(NOD_RESULT_ERR_OTHER, "seek before the start");
    return -1;
  }
  handle->position = static_cast<uint64_t>(base + offset);
  return static_cast<int64_t>(handle->position);
}

NodResult nod_disc_header(const NodHandle* disc, NodDiscHeader* out) {
  if (disc == nullptr || disc->kind != NodHandle::Kind::Disc || out == nullptr) {
    return fail(NOD_RESULT_ERR_INVALID_HANDLE, "not a disc handle");
  }
  static_assert(sizeof(NodDiscHeader) == 0x400);
  if (!disc->disc->readExact(0, out, sizeof(*out))) {
    return fail(NOD_RESULT_ERR_IO, "failed to read the disc header");
  }
  return NOD_RESULT_OK;
}

NodResult nod_disc_meta(const NodHandle* disc, NodDiscMeta* out) {
  if (disc == nullptr || disc->kind != NodHandle::Kind::Disc || out == nullptr) {
    return fail(NOD_RESULT_ERR_INVALID_HANDLE, "not a disc handle");
  }
  *out = NodDiscMeta{};
  out->format = disc->disc->cisoBlockSize != 0 ? NOD_FORMAT_CISO : NOD_FORMAT_ISO;
  out->lossless = true;
  out->disc_size = disc->disc->size;
  return NOD_RESULT_OK;
}

uint64_t nod_disc_size(const NodHandle* disc) {
  return disc != nullptr && disc->kind == NodHandle::Kind::Disc ? disc->disc->size : 0;
}

size_t nod_disc_partitions(const NodHandle* disc, NodPartitionInfo* out, size_t cap) {
  if (disc == nullptr || disc->kind != NodHandle::Kind::Disc) {
    return 0;
  }
  if (out != nullptr && cap > 0) {
    out[0] = NodPartitionInfo{.index = 0, .kind = NOD_PARTITION_KIND_DATA, .data_size = disc->disc->size};
  }
  return 1;
}

bool nod_partition_is_wii(const NodHandle* partition) {
  (void)partition;
  return false;
}

NodResult nod_partition_meta(const NodHandle* partition, NodPartitionMeta* out) {
  if (partition == nullptr || partition->kind != NodHandle::Kind::Partition || out == nullptr) {
    return fail(NOD_RESULT_ERR_INVALID_HANDLE, "not a partition handle");
  }
  const Partition& p = *partition->partition;
  *out = NodPartitionMeta{};
  out->raw_boot = {p.boot.data(), p.boot.size()};
  out->raw_bi2 = {p.bi2.data(), p.bi2.size()};
  out->raw_apploader = {p.apploader.data(), p.apploader.size()};
  out->raw_dol = {p.dol.data(), p.dol.size()};
  out->raw_fst = {p.fst.data(), size_t(p.entries) * 12 + p.namesSize};
  return NOD_RESULT_OK;
}

uint32_t nod_partition_find_file(const NodHandle* partition, const char* path, NodNodeKind* out_kind,
                                 uint32_t* out_length) {
  if (partition == nullptr || partition->kind != NodHandle::Kind::Partition || path == nullptr) {
    return NOD_FST_STOP;
  }
  const Partition& p = *partition->partition;
  uint32_t index = 0;        // the directory searched: the root
  uint32_t end = p.entries;  // its end
  FstEntry entry{.directory = true, .name = "", .offset = 0, .size = p.entries};
  while (*path != '\0') {
    while (*path == '/') {
      path++;
    }
    const char* slash = strchr(path, '/');
    const size_t length = slash != nullptr ? size_t(slash - path) : strlen(path);
    if (length == 0) {
      break;
    }
    if (!entry.directory) {
      return NOD_FST_STOP;  // a file is not a folder
    }
    bool found = false;
    for (uint32_t child = index + 1; child < end && child < p.entries;) {
      FstEntry candidate;
      if (!fstEntry(p, child, candidate)) {
        break;
      }
      if (sameName(candidate.name, path, length)) {
        index = child;
        entry = candidate;
        end = candidate.directory ? candidate.size : child + 1;
        found = true;
        break;
      }
      child = candidate.directory ? std::max(candidate.size, child + 1) : child + 1;
    }
    if (!found) {
      return NOD_FST_STOP;
    }
    path += length;
  }
  if (out_kind != nullptr) {
    *out_kind = entry.directory ? NOD_NODE_KIND_DIRECTORY : NOD_NODE_KIND_FILE;
  }
  if (out_length != nullptr) {
    *out_length = entry.directory ? 0 : entry.size;
  }
  return index;
}

void nod_partition_iterate_fst(const NodHandle* partition, NodFstCallback callback, void* user_data) {
  if (partition == nullptr || partition->kind != NodHandle::Kind::Partition || callback == nullptr) {
    return;
  }
  const Partition& p = *partition->partition;
  // The root, entry 0, is not reported
  for (uint32_t index = 1; index < p.entries;) {
    FstEntry entry;
    if (!fstEntry(p, index, entry)) {
      break;
    }
    const uint32_t next = callback(index, entry.directory ? NOD_NODE_KIND_DIRECTORY : NOD_NODE_KIND_FILE,
                                   entry.name, entry.size, user_data);
    if (next == NOD_FST_STOP || next <= index) {
      break;
    }
    index = next;
  }
}

}  // extern "C"
