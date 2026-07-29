#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "vemory/persist/RdbFormat.h"
#include "vemory/util/MappedFile.h"

namespace {

class TempFile {
 public:
  TempFile() {
    path_ = "/tmp/vemory_mapped_XXXXXX";
    const int fd = ::mkstemp(path_.data());
    EXPECT_GE(fd, 0);
    if (fd >= 0) {
      fd_ = fd;
    }
  }
  ~TempFile() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    if (!path_.empty()) {
      ::unlink(path_.c_str());
    }
  }
  bool Write(const void* data, std::size_t n) {
    return fd_ >= 0 && ::write(fd_, data, n) == static_cast<ssize_t>(n);
  }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
  int fd_ = -1;
};

std::vector<uint8_t> MakeMinimalHeader(const rdb::Header& h) {
  std::vector<uint8_t> buf(rdb::kHeaderBytes, 0);
  std::size_t off = 0;
  auto put = [&](const void* src, std::size_t n) {
    std::memcpy(buf.data() + off, src, n);
    off += n;
  };
  put(rdb::kMagic, 8);
  put(&h.version, sizeof(h.version));
  put(&h.flags, sizeof(h.flags));
  put(&h.dim, sizeof(h.dim));
  put(&h.next_id, sizeof(h.next_id));
  put(&h.pad, sizeof(h.pad));
  put(&h.kv_count, sizeof(h.kv_count));
  put(&h.node_count, sizeof(h.node_count));
  put(&h.toc[0], sizeof(h.toc));
  return buf;
}

}  // namespace

TEST(MappedFile, OpenReadRoundTrip) {
  TempFile f;
  const char payload[] = "hello-mmap";
  ASSERT_TRUE(f.Write(payload, sizeof(payload) - 1));
  {
    const int fd = ::open(f.path().c_str(), O_RDWR);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::fsync(fd), 0);
    ::close(fd);
  }

  MappedFile mapped;
  ASSERT_EQ(mapped.Open(f.path()), MappedFile::Status::kOk);
  ASSERT_EQ(mapped.size(), sizeof(payload) - 1);
  ASSERT_EQ(std::memcmp(mapped.data(), payload, mapped.size()), 0);
}

TEST(MappedFile, MissingFile) {
  MappedFile mapped;
  EXPECT_EQ(mapped.Open("/no/such/vemory_mapped_file"),
            MappedFile::Status::kIoError);
}

TEST(RdbFormat, ParseHeaderOk) {
  rdb::Header h;
  h.version = rdb::kVersion;
  h.kv_count = 1;
  h.node_count = 2;
  h.toc[0] = {rdb::kHeaderBytes, 8};
  h.toc[1] = {rdb::kHeaderBytes + 8, 16};
  h.toc[2] = {0, 0};
  auto bytes = MakeMinimalHeader(h);
  // Extend file image so TOC lengths fit.
  bytes.resize(rdb::kHeaderBytes + 8 + 16, 0);

  rdb::Header out;
  ASSERT_EQ(rdb::ParseHeader(bytes.data(), bytes.size(), &out),
            rdb::ParseStatus::kOk);
  EXPECT_EQ(out.kv_count, 1u);
  EXPECT_EQ(out.node_count, 2u);
  EXPECT_EQ(out.toc[0].offset, rdb::kHeaderBytes);
  EXPECT_EQ(out.toc[0].length, 8u);
}

TEST(RdbFormat, BadMagic) {
  auto bytes = MakeMinimalHeader(rdb::Header{});
  bytes[0] = 'X';
  rdb::Header out;
  EXPECT_EQ(rdb::ParseHeader(bytes.data(), bytes.size(), &out),
            rdb::ParseStatus::kError);
}

TEST(RdbFormat, Truncated) {
  std::vector<uint8_t> bytes(16, 0);
  rdb::Header out;
  EXPECT_EQ(rdb::ParseHeader(bytes.data(), bytes.size(), &out),
            rdb::ParseStatus::kIoError);
}

TEST(RdbFormat, TocOutOfBounds) {
  rdb::Header h;
  h.version = rdb::kVersion;
  h.toc[0] = {rdb::kHeaderBytes, 1000};  // length past end
  auto bytes = MakeMinimalHeader(h);
  rdb::Header out;
  EXPECT_EQ(rdb::ParseHeader(bytes.data(), bytes.size(), &out),
            rdb::ParseStatus::kError);
}
