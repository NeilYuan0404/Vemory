#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vemory/protocol/CommandType.h"

// How VSIM supplies the query vector.
enum class VsimMode : uint8_t {
  kNone = 0,
  kEle,
  kValues,
};

// One client request after RESP argv → command mapping.
struct RequestContext {
  int client_fd = -1;
  CommandType cmd = CommandType::kUnknown;
  std::string key;
  std::string element;
  std::vector<float> embed;
  // VSIM: neighbor count (default 10); WITHSCORES flag.
  std::size_t count = 10;
  bool with_scores = false;
  VsimMode vsim_mode = VsimMode::kNone;
  std::chrono::steady_clock::time_point recv_time{};

  enum class Status : uint8_t {
    kOk = 0,
    kEmpty,
    kUnknownCommand,
    kWrongArity,
    kBadValue,
  };

  // Expected shapes:
  //   VADD <key> VALUES <dim> <f1> … <fN> <element>
  //   VSIM <key> ELE <element> [COUNT <n>] [WITHSCORES]
  //   VSIM <key> VALUES <dim> <f1>…<fN> [COUNT <n>] [WITHSCORES]
  //   VDIM <key>
  //   VEMB <key> <element>
  //   VCARD <key>
  //   SET <key> <value>   (value stored in element)
  //   GET <key>
  //   DEL <key>
  //   PING [<message>]    (message in element)
  //   ECHO <message>      (message in element)
  static Status FromArgv(int client_fd,
                         const std::vector<std::string_view>& argv,
                         RequestContext* out);
};
