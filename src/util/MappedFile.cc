#include "vemory/util/MappedFile.h"

#include <cstring>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

MappedFile::~MappedFile() { Close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : path_(std::move(other.path_)),
      data_(other.data_),
      size_(other.size_),
      fd_(other.fd_) {
  other.data_ = nullptr;
  other.size_ = 0;
  other.fd_ = -1;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
  if (this != &other) {
    Close();
    path_ = std::move(other.path_);
    data_ = other.data_;
    size_ = other.size_;
    fd_ = other.fd_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.fd_ = -1;
  }
  return *this;
}

void MappedFile::Close() {
  if (data_ != nullptr && size_ > 0) {
    ::munmap(data_, size_);
  }
  data_ = nullptr;
  size_ = 0;
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  path_.clear();
}

MappedFile::Status MappedFile::Open(std::string_view path) {
  if (path.empty()) {
    return Status::kBadValue;
  }
  Close();
  path_.assign(path.data(), path.size());
  fd_ = ::open(path_.c_str(), O_RDONLY);
  if (fd_ < 0) {
    path_.clear();
    return Status::kIoError;
  }
  struct stat st {};
  if (::fstat(fd_, &st) != 0 || st.st_size < 0) {
    Close();
    return Status::kIoError;
  }
  const auto file_size = static_cast<std::size_t>(st.st_size);
  if (file_size == 0) {
    // Empty file: valid map of length 0 (no mmap).
    size_ = 0;
    data_ = nullptr;
    return Status::kOk;
  }
  void* addr = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd_, 0);
  if (addr == MAP_FAILED) {
    Close();
    return Status::kIoError;
  }
  data_ = static_cast<uint8_t*>(addr);
  size_ = file_size;
  return Status::kOk;
}
