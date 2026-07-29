#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Read-only mmap of a whole file (PROT_READ, MAP_PRIVATE).
class MappedFile {
 public:
  enum class Status : uint8_t {
    kOk = 0,
    kBadValue,
    kIoError,
  };

  MappedFile() = default;
  ~MappedFile();

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&& other) noexcept;
  MappedFile& operator=(MappedFile&& other) noexcept;

  Status Open(std::string_view path);
  void Close();

  const uint8_t* data() const { return data_; }
  std::size_t size() const { return size_; }
  bool empty() const { return data_ == nullptr || size_ == 0; }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
  uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  int fd_ = -1;
};
