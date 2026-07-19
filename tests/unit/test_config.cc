#include <gtest/gtest.h>

#include <unistd.h>

#include <string>

#include "vemory/util/Config.h"

namespace {

class TempIni {
 public:
  explicit TempIni(const std::string& contents) {
    path_ = "/tmp/vemory_test_config_XXXXXX.ini";
    // mkstemps needs editable buffer
    std::string tmpl = path_;
    const int fd = ::mkstemps(tmpl.data(), 4);  // keep ".ini"
    EXPECT_GE(fd, 0);
    if (fd >= 0) {
      path_ = tmpl;
      const ssize_t n =
          ::write(fd, contents.data(), static_cast<size_t>(contents.size()));
      EXPECT_EQ(n, static_cast<ssize_t>(contents.size()));
      ::close(fd);
    }
  }

  ~TempIni() {
    if (!path_.empty()) {
      ::unlink(path_.c_str());
    }
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace

TEST(Config, Defaults) {
  vemory::Config cfg;
  EXPECT_EQ(cfg.port, 6379);
  EXPECT_EQ(cfg.bind, "0.0.0.0");
  EXPECT_EQ(cfg.log_level, "info");
  EXPECT_EQ(cfg.kv_reserve, 100000u);
  EXPECT_EQ(cfg.default_capacity, 1024u);
}

TEST(Config, LoadHappyPath) {
  TempIni file(R"(
# comment
[server]
port = 6380
bind = 127.0.0.1

[logging]
level = debug

[storage]
kv_reserve = 42

[index]
default_capacity = 2048
)");
  vemory::Config cfg;
  std::string err;
  ASSERT_TRUE(vemory::LoadConfig(file.path(), &cfg, &err)) << err;
  EXPECT_TRUE(cfg.warnings.empty());
  EXPECT_EQ(cfg.port, 6380);
  EXPECT_EQ(cfg.bind, "127.0.0.1");
  EXPECT_EQ(cfg.log_level, "debug");
  EXPECT_EQ(cfg.kv_reserve, 42u);
  EXPECT_EQ(cfg.default_capacity, 2048u);
}

TEST(Config, CaseInsensitiveKeysAndSections) {
  TempIni file(R"(
[Server]
Port = 9001
BIND = 10.0.0.1
[LOGGING]
LEVEL = WARN
)");
  vemory::Config cfg;
  std::string err;
  ASSERT_TRUE(vemory::LoadConfig(file.path(), &cfg, &err)) << err;
  EXPECT_EQ(cfg.port, 9001);
  EXPECT_EQ(cfg.bind, "10.0.0.1");
  EXPECT_EQ(cfg.log_level, "warn");
}

TEST(Config, SemicolonCommentsAndBlankLines) {
  TempIni file(R"(
; leading comment

[server]
port = 7000
; trailing section
)");
  vemory::Config cfg;
  std::string err;
  ASSERT_TRUE(vemory::LoadConfig(file.path(), &cfg, &err)) << err;
  EXPECT_EQ(cfg.port, 7000);
}

TEST(Config, UnknownSectionAndKeyWarn) {
  TempIni file(R"(
[server]
port = 6379
extra = 1
[future]
foo = bar
)");
  vemory::Config cfg;
  std::string err;
  ASSERT_TRUE(vemory::LoadConfig(file.path(), &cfg, &err)) << err;
  ASSERT_EQ(cfg.warnings.size(), 2u);
  EXPECT_NE(cfg.warnings[0].find("unknown key"), std::string::npos);
  EXPECT_NE(cfg.warnings[1].find("unknown section"), std::string::npos);
}

TEST(Config, MissingFile) {
  vemory::Config cfg;
  std::string err;
  EXPECT_FALSE(vemory::LoadConfig("/no/such/vemory.ini", &cfg, &err));
  EXPECT_FALSE(err.empty());
}

TEST(Config, BadPort) {
  TempIni file("[server]\nport = notanumber\n");
  vemory::Config cfg;
  std::string err;
  EXPECT_FALSE(vemory::LoadConfig(file.path(), &cfg, &err));
  EXPECT_NE(err.find("port"), std::string::npos);
}

TEST(Config, BadLogLevel) {
  TempIni file("[logging]\nlevel = verbose\n");
  vemory::Config cfg;
  std::string err;
  EXPECT_FALSE(vemory::LoadConfig(file.path(), &cfg, &err));
  EXPECT_NE(err.find("level"), std::string::npos);
}

TEST(Config, KeyOutsideSection) {
  TempIni file("port = 1\n");
  vemory::Config cfg;
  std::string err;
  EXPECT_FALSE(vemory::LoadConfig(file.path(), &cfg, &err));
  EXPECT_NE(err.find("section"), std::string::npos);
}

TEST(Config, IsValidLogLevel) {
  EXPECT_TRUE(vemory::IsValidLogLevel("info"));
  EXPECT_TRUE(vemory::IsValidLogLevel("off"));
  EXPECT_FALSE(vemory::IsValidLogLevel("verbose"));
  EXPECT_FALSE(vemory::IsValidLogLevel(""));
}
