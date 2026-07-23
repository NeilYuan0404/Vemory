#include "vemory/util/Config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace vemory {
namespace {

std::string Trim(std::string_view s) {
  std::size_t begin = 0;
  while (begin < s.size() &&
         std::isspace(static_cast<unsigned char>(s[begin]))) {
    ++begin;
  }
  std::size_t end = s.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return std::string(s.substr(begin, end - begin));
}

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool ParseUint(std::string_view text, unsigned long long* out) {
  if (text.empty() || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  const std::string tmp(text);
  const unsigned long long v = std::strtoull(tmp.c_str(), &end, 10);
  if (end == tmp.c_str() || *end != '\0') {
    return false;
  }
  *out = v;
  return true;
}

bool IsKnownSection(const std::string& section) {
  return section == "server" || section == "logging" || section == "storage" ||
         section == "index" || section == "persistence";
}

bool ParseBool(std::string_view text, bool* out) {
  if (out == nullptr) {
    return false;
  }
  const std::string lower = ToLower(std::string(text));
  if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
    *out = true;
    return true;
  }
  if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
    *out = false;
    return true;
  }
  return false;
}

bool ApplyKey(Config* cfg, const std::string& section, const std::string& key,
              const std::string& value, std::string* error) {
  if (!IsKnownSection(section)) {
    return true;  // warned once when the section header was seen
  }
  if (section == "server") {
    if (key == "port") {
      unsigned long long v = 0;
      if (!ParseUint(value, &v) || v == 0 || v > 65535) {
        if (error != nullptr) {
          *error = "invalid server.port: " + value;
        }
        return false;
      }
      cfg->port = static_cast<uint16_t>(v);
      return true;
    }
    if (key == "bind") {
      if (value.empty()) {
        if (error != nullptr) {
          *error = "invalid server.bind: empty";
        }
        return false;
      }
      cfg->bind = value;
      return true;
    }
  } else if (section == "logging") {
    if (key == "level") {
      const std::string level = ToLower(value);
      if (!IsValidLogLevel(level)) {
        if (error != nullptr) {
          *error = "invalid logging.level: " + value;
        }
        return false;
      }
      cfg->log_level = level;
      return true;
    }
  } else if (section == "storage") {
    if (key == "kv_reserve") {
      unsigned long long v = 0;
      if (!ParseUint(value, &v)) {
        if (error != nullptr) {
          *error = "invalid storage.kv_reserve: " + value;
        }
        return false;
      }
      cfg->kv_reserve = static_cast<std::size_t>(v);
      return true;
    }
  } else if (section == "index") {
    if (key == "default_capacity") {
      unsigned long long v = 0;
      if (!ParseUint(value, &v) || v == 0) {
        if (error != nullptr) {
          *error = "invalid index.default_capacity: " + value;
        }
        return false;
      }
      cfg->default_capacity = static_cast<std::size_t>(v);
      return true;
    }
  } else if (section == "persistence") {
    if (key == "dir") {
      cfg->persistence_dir = value;
      return true;
    }
    if (key == "load_on_startup") {
      bool v = false;
      if (!ParseBool(value, &v)) {
        if (error != nullptr) {
          *error = "invalid persistence.load_on_startup: " + value;
        }
        return false;
      }
      cfg->load_on_startup = v;
      return true;
    }
  }

  cfg->warnings.push_back("unknown key " + section + "." + key);
  return true;
}

}  // namespace

bool IsValidLogLevel(std::string_view level) {
  return level == "trace" || level == "debug" || level == "info" ||
         level == "warn" || level == "error" || level == "critical" ||
         level == "off";
}

bool LoadConfig(std::string_view path, Config* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "null Config";
    }
    return false;
  }

  Config cfg;
  cfg.warnings.clear();

  const std::string path_str(path);
  std::ifstream in(path_str);
  if (!in) {
    if (error != nullptr) {
      *error = "cannot open config file: " + std::string(path);
    }
    return false;
  }

  std::string section;
  std::unordered_set<std::string> warned_sections;
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    // Full-line comments with # or ;.
    std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }

    if (trimmed.front() == '[') {
      if (trimmed.back() != ']' || trimmed.size() < 3) {
        if (error != nullptr) {
          std::ostringstream oss;
          oss << "line " << line_no << ": bad section header";
          *error = oss.str();
        }
        return false;
      }
      section = ToLower(Trim(trimmed.substr(1, trimmed.size() - 2)));
      if (section.empty()) {
        if (error != nullptr) {
          std::ostringstream oss;
          oss << "line " << line_no << ": empty section name";
          *error = oss.str();
        }
        return false;
      }
      if (!IsKnownSection(section) &&
          warned_sections.insert(section).second) {
        cfg.warnings.push_back("unknown section [" + section + "]");
      }
      continue;
    }

    const auto eq = trimmed.find('=');
    if (eq == std::string::npos) {
      if (error != nullptr) {
        std::ostringstream oss;
        oss << "line " << line_no << ": expected key = value";
        *error = oss.str();
      }
      return false;
    }
    if (section.empty()) {
      if (error != nullptr) {
        std::ostringstream oss;
        oss << "line " << line_no << ": key outside of section";
        *error = oss.str();
      }
      return false;
    }

    const std::string key = ToLower(Trim(trimmed.substr(0, eq)));
    const std::string value = Trim(trimmed.substr(eq + 1));
    if (key.empty()) {
      if (error != nullptr) {
        std::ostringstream oss;
        oss << "line " << line_no << ": empty key";
        *error = oss.str();
      }
      return false;
    }

    if (!ApplyKey(&cfg, section, key, value, error)) {
      if (error != nullptr && !error->empty()) {
        std::ostringstream oss;
        oss << "line " << line_no << ": " << *error;
        *error = oss.str();
      }
      return false;
    }
  }

  *out = std::move(cfg);
  return true;
}

}  // namespace vemory
