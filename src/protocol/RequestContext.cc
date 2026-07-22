#include "vemory/protocol/RequestContext.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace {

bool ParseFloat(std::string_view s, float* out) {
  if (out == nullptr || s.empty()) {
    return false;
  }
  std::size_t i = 0;
  while (i < s.size() &&
         std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  if (i == s.size()) {
    return false;
  }
  char* end = nullptr;
  const std::string tmp(s.substr(i));
  const float v = std::strtof(tmp.c_str(), &end);
  if (end == tmp.c_str()) {
    return false;
  }
  while (*end != '\0' &&
         std::isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }
  *out = v;
  return true;
}

void ClearOut(RequestContext* out, int client_fd, CommandType cmd) {
  out->client_fd = client_fd;
  out->cmd = cmd;
  out->vector_blob.clear();
  out->user_key.clear();
  out->question.clear();
  out->answer.clear();
  out->threshold = 0.f;
  out->key.clear();
  out->element.clear();
  out->recv_time = {};
}

}  // namespace

RequestContext::Status RequestContext::FromArgv(
    int client_fd, const std::vector<std::string_view>& argv,
    RequestContext* out) {
  if (out == nullptr) {
    return Status::kEmpty;
  }
  if (argv.empty()) {
    return Status::kEmpty;
  }

  const CommandType cmd = ParseCommandType(argv[0]);
  auto fail = [&](Status st) {
    ClearOut(out, client_fd, cmd == CommandType::kUnknown ? CommandType::kUnknown
                                                          : cmd);
    return st;
  };

  if (cmd == CommandType::kUnknown) {
    return fail(Status::kUnknownCommand);
  }

  switch (cmd) {
    case CommandType::kVset: {
      // VSET <vector_blob> <user_key> <question> <answer>
      if (argv.size() != 5) {
        return fail(Status::kWrongArity);
      }
      if (argv[2].empty()) {
        return fail(Status::kBadValue);
      }
      ClearOut(out, client_fd, cmd);
      out->vector_blob.assign(argv[1].data(), argv[1].size());
      out->user_key.assign(argv[2].data(), argv[2].size());
      out->question.assign(argv[3].data(), argv[3].size());
      out->answer.assign(argv[4].data(), argv[4].size());
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kVget: {
      // VGET <query_vector_blob> <threshold>
      if (argv.size() != 3) {
        return fail(Status::kWrongArity);
      }
      float thr = 0.f;
      if (!ParseFloat(argv[2], &thr)) {
        return fail(Status::kBadValue);
      }
      ClearOut(out, client_fd, cmd);
      out->vector_blob.assign(argv[1].data(), argv[1].size());
      out->threshold = thr;
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kVdel: {
      if (argv.size() != 2) {
        return fail(Status::kWrongArity);
      }
      if (argv[1].empty()) {
        return fail(Status::kBadValue);
      }
      ClearOut(out, client_fd, cmd);
      out->user_key.assign(argv[1].data(), argv[1].size());
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kSet: {
      if (argv.size() != 3) {
        return fail(Status::kWrongArity);
      }
      ClearOut(out, client_fd, cmd);
      out->key.assign(argv[1].data(), argv[1].size());
      out->element.assign(argv[2].data(), argv[2].size());
      if (out->key.empty()) {
        return fail(Status::kBadValue);
      }
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kGet:
    case CommandType::kDel: {
      if (argv.size() != 2) {
        return fail(Status::kWrongArity);
      }
      ClearOut(out, client_fd, cmd);
      out->key.assign(argv[1].data(), argv[1].size());
      if (out->key.empty()) {
        return fail(Status::kBadValue);
      }
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kPing: {
      if (argv.size() > 2) {
        return fail(Status::kWrongArity);
      }
      ClearOut(out, client_fd, cmd);
      if (argv.size() == 2) {
        out->element.assign(argv[1].data(), argv[1].size());
      }
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kEcho: {
      if (argv.size() != 2) {
        return fail(Status::kWrongArity);
      }
      ClearOut(out, client_fd, cmd);
      out->element.assign(argv[1].data(), argv[1].size());
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kUnknown:
      break;
  }
  return fail(Status::kUnknownCommand);
}
