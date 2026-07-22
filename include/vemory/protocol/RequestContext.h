#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vemory/protocol/CommandType.h"

// One client request after RESP tokens → command mapping.
struct RequestContext {
  int client_fd = -1;
  CommandType cmd = CommandType::kUnknown;

  // Semantic cache (VSET/VGET/VDEL).
  std::string vector_blob;
  std::string user_key;
  std::string question;
  std::string answer;
  float threshold = 0.f;

  // String KVS / assist: SET/GET/DEL key; value or message in element.
  std::string key;
  std::string element;

  std::chrono::steady_clock::time_point recv_time{};

  enum class Status : uint8_t {
    kOk = 0,
    kEmpty,
    kUnknownCommand,
    kWrongArity,
    kBadValue,
  };

  // Expected shapes:
  //   VSET <vector_blob> <user_key> <question> <answer>
  //   VGET <query_vector_blob> <threshold>
  //   VDEL <user_key>
  //   SET <key> <value>   (value stored in element)
  //   GET <key>
  //   DEL <key>
  //   PING [<message>]    (message in element)
  //   ECHO <message>      (message in element)
  static Status FromTokens(int client_fd,
                           const std::vector<std::string_view>& tokens,
                           RequestContext* out);
};
