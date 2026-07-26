#include "vemory/persist/SnapshotManager.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace {

bool WriteExact(FILE* fp, const void* data, std::size_t n) {
  return std::fwrite(data, 1, n, fp) == n;
}

bool ReadExact(FILE* fp, void* data, std::size_t n) {
  return std::fread(data, 1, n, fp) == n;
}

}  // namespace

SnapshotManager::SnapshotManager(VNodeIndex* vnode_index, KvStore* kv,
                                 std::string dir)
    : vnode_index_(vnode_index), kv_(kv), dir_(std::move(dir)) {}

SnapshotManager::~SnapshotManager() {
  if (reap_timer_ != nullptr) {
    Timer::GetInstance()->DelTimeout(reap_timer_);
    reap_timer_ = nullptr;
  }
  if (child_pid_ > 0) {
    int status = 0;
    ::waitpid(child_pid_, &status, 0);
    child_pid_ = -1;
  }
}

std::string SnapshotManager::Path(std::string_view name) const {
  std::filesystem::path p(dir_);
  p /= name;
  return p.string();
}

SnapshotManager::Status SnapshotManager::FsyncFile(FILE* fp) const {
  if (fp == nullptr) {
    return Status::kBadValue;
  }
  if (std::fflush(fp) != 0) {
    return Status::kIoError;
  }
  const int fd = ::fileno(fp);
  if (fd < 0 || ::fsync(fd) != 0) {
    return Status::kIoError;
  }
  return Status::kOk;
}

SnapshotManager::Status SnapshotManager::AtomicRename(
    const std::string& tmp, const std::string& final_path) const {
  if (::rename(tmp.c_str(), final_path.c_str()) != 0) {
    return Status::kIoError;
  }
  return Status::kOk;
}

void SnapshotManager::RemoveLegacyDumpFiles() const {
  static constexpr const char* kLegacy[] = {
      "dump.meta",         "dump.kv",         "dump.nodes",
      "dump.usearch",      "dump.meta.tmp",   "dump.kv.tmp",
      "dump.nodes.tmp",    "dump.usearch.tmp",
  };
  for (const char* name : kLegacy) {
    std::error_code ec;
    std::filesystem::remove(Path(name), ec);
  }
}

SnapshotManager::Status SnapshotManager::WriteHeader(FILE* fp,
                                                    const Header& header) const {
  if (fp == nullptr) {
    return Status::kBadValue;
  }
  if (std::fseek(fp, 0, SEEK_SET) != 0) {
    return Status::kIoError;
  }
  char magic[8] = {};
  std::memcpy(magic, kMagic, 8);
  const bool ok =
      WriteExact(fp, magic, sizeof(magic)) &&
      WriteExact(fp, &header.version, sizeof(header.version)) &&
      WriteExact(fp, &header.flags, sizeof(header.flags)) &&
      WriteExact(fp, &header.dim, sizeof(header.dim)) &&
      WriteExact(fp, &header.next_id, sizeof(header.next_id)) &&
      WriteExact(fp, &header.pad, sizeof(header.pad)) &&
      WriteExact(fp, &header.kv_count, sizeof(header.kv_count)) &&
      WriteExact(fp, &header.node_count, sizeof(header.node_count)) &&
      WriteExact(fp, &header.toc[0], sizeof(header.toc));
  return ok ? Status::kOk : Status::kIoError;
}

SnapshotManager::Status SnapshotManager::ReadHeader(FILE* fp,
                                                   Header* header) const {
  if (fp == nullptr || header == nullptr) {
    return Status::kBadValue;
  }
  if (std::fseek(fp, 0, SEEK_SET) != 0) {
    return Status::kIoError;
  }
  char magic[8] = {};
  Header h;
  const bool ok =
      ReadExact(fp, magic, sizeof(magic)) &&
      ReadExact(fp, &h.version, sizeof(h.version)) &&
      ReadExact(fp, &h.flags, sizeof(h.flags)) &&
      ReadExact(fp, &h.dim, sizeof(h.dim)) &&
      ReadExact(fp, &h.next_id, sizeof(h.next_id)) &&
      ReadExact(fp, &h.pad, sizeof(h.pad)) &&
      ReadExact(fp, &h.kv_count, sizeof(h.kv_count)) &&
      ReadExact(fp, &h.node_count, sizeof(h.node_count)) &&
      ReadExact(fp, &h.toc[0], sizeof(h.toc));
  if (!ok) {
    return Status::kIoError;
  }
  if (std::memcmp(magic, kMagic, 8) != 0 || h.version != kVersion) {
    return Status::kError;
  }
  *header = h;
  return Status::kOk;
}

SnapshotManager::Status SnapshotManager::SaveToDir() const {
  if (dir_.empty() || vnode_index_ == nullptr || kv_ == nullptr) {
    return Status::kNotConfigured;
  }

  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) {
    return Status::kIoError;
  }

  const std::string tmp_path = Path(kRdbTmpName);
  const std::string final_path = Path(kRdbName);

  FILE* fp = std::fopen(tmp_path.c_str(), "wb+");
  if (fp == nullptr) {
    return Status::kIoError;
  }

  Header header;
  header.version = kVersion;
  header.dim = vnode_index_->dimensions();
  header.next_id = vnode_index_->next_id();
  header.kv_count = kv_->size();
  header.node_count = vnode_index_->node_count();

  // Reserve header; payloads start immediately after.
  if (std::fseek(fp, kHeaderBytes, SEEK_SET) != 0) {
    std::fclose(fp);
    return Status::kIoError;
  }

  // KV segment
  {
    const long start = std::ftell(fp);
    if (start < 0) {
      std::fclose(fp);
      return Status::kIoError;
    }
    if (kv_->Dump(fp) != KvStore::Status::kOk) {
      std::fclose(fp);
      return Status::kIoError;
    }
    const long end = std::ftell(fp);
    if (end < 0 || end < start) {
      std::fclose(fp);
      return Status::kIoError;
    }
    header.toc[0].offset = static_cast<uint64_t>(start);
    header.toc[0].length = static_cast<uint64_t>(end - start);
  }

  // NODES segment
  {
    const long start = std::ftell(fp);
    if (start < 0) {
      std::fclose(fp);
      return Status::kIoError;
    }
    if (vnode_index_->DumpNodes(fp) != VNodeIndex::Status::kOk) {
      std::fclose(fp);
      return Status::kIoError;
    }
    const long end = std::ftell(fp);
    if (end < 0 || end < start) {
      std::fclose(fp);
      return Status::kIoError;
    }
    header.toc[1].offset = static_cast<uint64_t>(start);
    header.toc[1].length = static_cast<uint64_t>(end - start);
  }

  // USEARCH segment (optional when dim==0)
  if (header.dim > 0) {
    const long start = std::ftell(fp);
    if (start < 0) {
      std::fclose(fp);
      return Status::kIoError;
    }
    if (vnode_index_->SaveIndex(fp) != VNodeIndex::Status::kOk) {
      std::fclose(fp);
      return Status::kIoError;
    }
    const long end = std::ftell(fp);
    if (end < 0 || end < start) {
      std::fclose(fp);
      return Status::kIoError;
    }
    header.toc[2].offset = static_cast<uint64_t>(start);
    header.toc[2].length = static_cast<uint64_t>(end - start);
    if (header.toc[2].length > 0) {
      header.flags |= kFlagHasUsearch;
    }
  }

  if (WriteHeader(fp, header) != Status::kOk) {
    std::fclose(fp);
    return Status::kIoError;
  }
  const auto st = FsyncFile(fp);
  std::fclose(fp);
  if (st != Status::kOk) {
    return st;
  }

  if (AtomicRename(tmp_path, final_path) != Status::kOk) {
    return Status::kIoError;
  }
  RemoveLegacyDumpFiles();
  return Status::kOk;
}

SnapshotManager::Status SnapshotManager::Load() {
  if (dir_.empty() || vnode_index_ == nullptr || kv_ == nullptr) {
    return Status::kNotConfigured;
  }

  FILE* fp = std::fopen(Path(kRdbName).c_str(), "rb");
  if (fp == nullptr) {
    return Status::kIoError;
  }

  Header header;
  const auto hst = ReadHeader(fp, &header);
  if (hst != Status::kOk) {
    std::fclose(fp);
    return hst;
  }

  // Load KV
  if (std::fseek(fp, static_cast<long>(header.toc[0].offset), SEEK_SET) != 0) {
    std::fclose(fp);
    return Status::kIoError;
  }
  kv_->Clear();
  if (kv_->Load(fp) != KvStore::Status::kOk) {
    std::fclose(fp);
    return Status::kIoError;
  }
  if (kv_->size() != static_cast<std::size_t>(header.kv_count)) {
    std::fclose(fp);
    return Status::kError;
  }

  // Load nodes (clears vnode storage)
  if (std::fseek(fp, static_cast<long>(header.toc[1].offset), SEEK_SET) != 0) {
    std::fclose(fp);
    return Status::kIoError;
  }
  vnode_index_->Clear();
  if (vnode_index_->LoadNodes(fp, header.node_count,
                              static_cast<uint16_t>(header.next_id)) !=
      VNodeIndex::Status::kOk) {
    std::fclose(fp);
    return Status::kIoError;
  }
  if (vnode_index_->node_count() !=
      static_cast<std::size_t>(header.node_count)) {
    std::fclose(fp);
    return Status::kError;
  }

  if (header.dim > 0) {
    if (header.toc[2].length == 0) {
      std::fclose(fp);
      return Status::kError;
    }
    if (std::fseek(fp, static_cast<long>(header.toc[2].offset), SEEK_SET) !=
        0) {
      std::fclose(fp);
      return Status::kIoError;
    }
    if (vnode_index_->LoadIndex(fp, static_cast<std::size_t>(header.dim)) !=
        VNodeIndex::Status::kOk) {
      std::fclose(fp);
      return Status::kIoError;
    }
  }

  std::fclose(fp);
  return Status::kOk;
}

void SnapshotManager::EnsureReapTimer() {
  if (reap_timer_ != nullptr) {
    return;
  }
  reap_timer_ = Timer::GetInstance()->AddTimeout(100, [this]() {
    reap_timer_ = nullptr;
    ReapSaveChild();
    if (child_pid_ > 0) {
      EnsureReapTimer();
    }
  });
}

SnapshotManager::Status SnapshotManager::BackgroundSave() {
  if (dir_.empty()) {
    return Status::kNotConfigured;
  }
  if (child_pid_ > 0) {
    return Status::kInProgress;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    return Status::kError;
  }
  if (pid == 0) {
    // Child: dump and exit. Avoid returning into parent's event loop.
    const Status st = SaveToDir();
    _exit(st == Status::kOk ? 0 : 1);
  }

  child_pid_ = pid;
  EnsureReapTimer();
  spdlog::info("Background SAVE started pid={}", static_cast<int>(pid));
  return Status::kOk;
}

void SnapshotManager::ReapSaveChild() {
  if (child_pid_ <= 0) {
    return;
  }
  int status = 0;
  const pid_t r = ::waitpid(child_pid_, &status, WNOHANG);
  if (r == 0) {
    return;  // still running
  }
  if (r < 0) {
    if (errno == ECHILD) {
      child_pid_ = -1;
    }
    return;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    spdlog::info("Background SAVE finished ok pid={}", static_cast<int>(r));
  } else {
    spdlog::error("Background SAVE failed pid={} status={}",
                  static_cast<int>(r), status);
  }
  child_pid_ = -1;
}
