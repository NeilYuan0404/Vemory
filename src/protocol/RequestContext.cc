#include "vemory/protocol/RequestContext.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const auto ca = static_cast<unsigned char>(a[i]);
    const auto cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb)) {
      return false;
    }
  }
  return true;
}

bool ParseUInt(std::string_view s, std::size_t* out) {
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
  const unsigned long long v = std::strtoull(tmp.c_str(), &end, 10);
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
  *out = static_cast<std::size_t>(v);
  return true;
}

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
  out->key.clear();
  out->element.clear();
  out->embed.clear();
  out->count = 10;
  out->with_scores = false;
  out->vsim_mode = VsimMode::kNone;
  out->recv_time = {};
}

// Parse trailing [COUNT n] [WITHSCORES] from argv[start..).
bool ParseVsimOptions(const std::vector<std::string_view>& argv,
                      std::size_t start, RequestContext* out) {
  std::size_t i = start;
  while (i < argv.size()) {
    if (EqualsIgnoreCase(argv[i], "COUNT")) {
      if (i + 1 >= argv.size()) {
        return false;
      }
      std::size_t n = 0;
      if (!ParseUInt(argv[i + 1], &n) || n == 0) {
        return false;
      }
      out->count = n;
      i += 2;
      continue;
    }
    if (EqualsIgnoreCase(argv[i], "WITHSCORES")) {
      out->with_scores = true;
      ++i;
      continue;
    }
    return false;
  }
  return true;
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
  if (cmd == CommandType::kUnknown) {
    ClearOut(out, client_fd, CommandType::kUnknown);
    return Status::kUnknownCommand;
  }

  auto fail = [&](Status st) {
    ClearOut(out, client_fd, cmd);
    return st;
  };

  switch (cmd) {
    case CommandType::kVadd: {
      // VADD key VALUES dim f1…fN element
      if (argv.size() < 5) {
        return fail(Status::kWrongArity);
      }
      if (!EqualsIgnoreCase(argv[2], "VALUES")) {
        return fail(Status::kBadValue);
      }
      std::size_t dim = 0;
      if (!ParseUInt(argv[3], &dim) || dim == 0) {
        return fail(Status::kBadValue);
      }
      // argv: [0]=VADD [1]=key [2]=VALUES [3]=dim [4..4+dim-1]=floats
      // [4+dim]=element  → size must be 5+dim
      if (argv.size() != 5 + dim) {
        return fail(Status::kWrongArity);
      }
      std::vector<float> embed;
      embed.reserve(dim);
      for (std::size_t i = 0; i < dim; ++i) {
        float f = 0.f;
        if (!ParseFloat(argv[4 + i], &f)) {
          return fail(Status::kBadValue);
        }
        embed.push_back(f);
      }
      ClearOut(out, client_fd, cmd);
      out->key.assign(argv[1].data(), argv[1].size());
      out->element.assign(argv[4 + dim].data(), argv[4 + dim].size());
      if (out->key.empty() || out->element.empty()) {
        return fail(Status::kBadValue);
      }
      out->embed = std::move(embed);
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kVsim: {
      // VSIM key ELE element [opts...]
      // VSIM key VALUES dim f1…fN [opts...]
      if (argv.size() < 4) {
        return fail(Status::kWrongArity);
      }
      ClearOut(out, client_fd, cmd);
      out->key.assign(argv[1].data(), argv[1].size());
      if (out->key.empty()) {
        return fail(Status::kBadValue);
      }

      if (EqualsIgnoreCase(argv[2], "ELE")) {
        out->vsim_mode = VsimMode::kEle;
        out->element.assign(argv[3].data(), argv[3].size());
        if (out->element.empty()) {
          return fail(Status::kBadValue);
        }
        if (!ParseVsimOptions(argv, 4, out)) {
          return fail(Status::kBadValue);
        }
        out->recv_time = std::chrono::steady_clock::now();
        return Status::kOk;
      }

      if (EqualsIgnoreCase(argv[2], "VALUES")) {
        out->vsim_mode = VsimMode::kValues;
        std::size_t dim = 0;
        if (!ParseUInt(argv[3], &dim) || dim == 0) {
          return fail(Status::kBadValue);
        }
        // Need at least: VSIM key VALUES dim f1…fN  → 4+dim
        if (argv.size() < 4 + dim) {
          return fail(Status::kWrongArity);
        }
        out->embed.reserve(dim);
        for (std::size_t i = 0; i < dim; ++i) {
          float f = 0.f;
          if (!ParseFloat(argv[4 + i], &f)) {
            return fail(Status::kBadValue);
          }
          out->embed.push_back(f);
        }
        if (!ParseVsimOptions(argv, 4 + dim, out)) {
          return fail(Status::kBadValue);
        }
        out->recv_time = std::chrono::steady_clock::now();
        return Status::kOk;
      }

      return fail(Status::kBadValue);
    }
    case CommandType::kVdim:
    case CommandType::kVcard: {
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
    case CommandType::kVemb: {
      if (argv.size() != 3) {
        return fail(Status::kWrongArity);
      }
      ClearOut(out, client_fd, cmd);
      out->key.assign(argv[1].data(), argv[1].size());
      out->element.assign(argv[2].data(), argv[2].size());
      if (out->key.empty() || out->element.empty()) {
        return fail(Status::kBadValue);
      }
      out->recv_time = std::chrono::steady_clock::now();
      return Status::kOk;
    }
    case CommandType::kSet: {
      // SET key value  (value → element for now)
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
      // PING | PING <message>
      if (argv.size() != 1 && argv.size() != 2) {
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
