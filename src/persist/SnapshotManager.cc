#include "vemory/persist/SnapshotManager.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
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

SnapshotManager::Status SnapshotManager::WriteMeta(const std::string& path,
                                                   const Meta& meta) const {
  FILE* fp = std::fopen(path.c_str(), "wb");
  if (fp == nullptr) {
    return Status::kIoError;
  }
  char magic[8] = {};
  std::memcpy(magic, kMagic, 8);
  const bool ok = WriteExact(fp, magic, sizeof(magic)) &&
                  WriteExact(fp, &meta.version, sizeof(meta.version)) &&
                  WriteExact(fp, &meta.dim, sizeof(meta.dim)) &&
                  WriteExact(fp, &meta.next_id, sizeof(meta.next_id)) &&
                  WriteExact(fp, &meta.kv_count, sizeof(meta.kv_count)) &&
                  WriteExact(fp, &meta.node_count, sizeof(meta.node_count));
  if (!ok) {
    std::fclose(fp);
    return Status::kIoError;
  }
  const auto st = FsyncFile(fp);
  std::fclose(fp);
  return st;
}

SnapshotManager::Status SnapshotManager::ReadMeta(const std::string& path,
                                                  Meta* meta) const {
  if (meta == nullptr) {
    return Status::kBadValue;
  }
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    return Status::kIoError;
  }
  char magic[8] = {};
  Meta m;
  const bool ok = ReadExact(fp, magic, sizeof(magic)) &&
                  ReadExact(fp, &m.version, sizeof(m.version)) &&
                  ReadExact(fp, &m.dim, sizeof(m.dim)) &&
                  ReadExact(fp, &m.next_id, sizeof(m.next_id)) &&
                  ReadExact(fp, &m.kv_count, sizeof(m.kv_count)) &&
                  ReadExact(fp, &m.node_count, sizeof(m.node_count));
  std::fclose(fp);
  if (!ok) {
    return Status::kIoError;
  }
  if (std::memcmp(magic, kMagic, 8) != 0 || m.version != kVersion) {
    return Status::kError;
  }
  *meta = m;
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

  const std::string kv_tmp = Path("dump.kv.tmp");
  const std::string nodes_tmp = Path("dump.nodes.tmp");
  const std::string usearch_tmp = Path("dump.usearch.tmp");
  const std::string meta_tmp = Path("dump.meta.tmp");

  const std::string kv_final = Path("dump.kv");
  const std::string nodes_final = Path("dump.nodes");
  const std::string usearch_final = Path("dump.usearch");
  const std::string meta_final = Path("dump.meta");

  Meta meta;
  meta.version = kVersion;
  meta.dim = vnode_index_->dimensions();
  meta.next_id = vnode_index_->next_id();
  meta.kv_count = kv_->size();
  meta.node_count = vnode_index_->node_count();

  // dump.kv.tmp
  {
    FILE* fp = std::fopen(kv_tmp.c_str(), "wb");
    if (fp == nullptr) {
      return Status::kIoError;
    }
    if (kv_->Dump(fp) != KvStore::Status::kOk) {
      std::fclose(fp);
      return Status::kIoError;
    }
    const auto st = FsyncFile(fp);
    std::fclose(fp);
    if (st != Status::kOk) {
      return st;
    }
  }

  // dump.nodes.tmp
  {
    FILE* fp = std::fopen(nodes_tmp.c_str(), "wb");
    if (fp == nullptr) {
      return Status::kIoError;
    }
    if (vnode_index_->DumpNodes(fp) != VNodeIndex::Status::kOk) {
      std::fclose(fp);
      return Status::kIoError;
    }
    const auto st = FsyncFile(fp);
    std::fclose(fp);
    if (st != Status::kOk) {
      return st;
    }
  }

  // dump.usearch.tmp (optional when dim==0)
  if (meta.dim > 0) {
    if (vnode_index_->SaveIndex(usearch_tmp.c_str()) != VNodeIndex::Status::kOk) {
      return Status::kIoError;
    }
    // fsync via open/fsync/close
    const int fd = ::open(usearch_tmp.c_str(), O_RDONLY);
    if (fd < 0 || ::fsync(fd) != 0) {
      if (fd >= 0) {
        ::close(fd);
      }
      return Status::kIoError;
    }
    ::close(fd);
  }

  // dump.meta.tmp last among temps
  if (WriteMeta(meta_tmp, meta) != Status::kOk) {
    return Status::kIoError;
  }

  // Rename payload files first; meta last = commit point.
  if (AtomicRename(kv_tmp, kv_final) != Status::kOk ||
      AtomicRename(nodes_tmp, nodes_final) != Status::kOk) {
    return Status::kIoError;
  }
  if (meta.dim > 0) {
    if (AtomicRename(usearch_tmp, usearch_final) != Status::kOk) {
      return Status::kIoError;
    }
  } else {
    std::error_code rm_ec;
    std::filesystem::remove(usearch_final, rm_ec);
  }
  if (AtomicRename(meta_tmp, meta_final) != Status::kOk) {
    return Status::kIoError;
  }
  return Status::kOk;
}

SnapshotManager::Status SnapshotManager::Load() {
  if (dir_.empty() || vnode_index_ == nullptr || kv_ == nullptr) {
    return Status::kNotConfigured;
  }

  const std::string meta_path = Path("dump.meta");
  Meta meta;
  const auto mst = ReadMeta(meta_path, &meta);
  if (mst != Status::kOk) {
    return mst;
  }

  // Load KV
  {
    FILE* fp = std::fopen(Path("dump.kv").c_str(), "rb");
    if (fp == nullptr) {
      return Status::kIoError;
    }
    kv_->Clear();
    const auto st = kv_->Load(fp);
    std::fclose(fp);
    if (st != KvStore::Status::kOk) {
      return Status::kIoError;
    }
    if (kv_->size() != static_cast<std::size_t>(meta.kv_count)) {
      return Status::kError;
    }
  }

  // Load nodes (clears vnode storage)
  vnode_index_->Clear();
  {
    FILE* fp = std::fopen(Path("dump.nodes").c_str(), "rb");
    if (fp == nullptr) {
      return Status::kIoError;
    }
    const auto st =
        vnode_index_->LoadNodes(fp, meta.node_count,
                                static_cast<uint16_t>(meta.next_id));
    std::fclose(fp);
    if (st != VNodeIndex::Status::kOk) {
      return Status::kIoError;
    }
    if (vnode_index_->node_count() !=
        static_cast<std::size_t>(meta.node_count)) {
      return Status::kError;
    }
  }

  if (meta.dim > 0) {
    if (vnode_index_->LoadIndex(Path("dump.usearch").c_str(),
                                static_cast<std::size_t>(meta.dim)) !=
        VNodeIndex::Status::kOk) {
      return Status::kIoError;
    }
  }

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
