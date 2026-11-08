#include "auth.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "json.h"
#include "platform.h"

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace karta {
namespace {

std::string auth_directory() {
  return (std::filesystem::path(home_directory()) / ".karta").string();
}

// Creates the file with 0600 from the very first byte written; std::ofstream
// would briefly create it with the process umask (typically 0644).
bool write_private_file(const std::string& path, const std::string& content) {
#ifdef _WIN32
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  stream << content;
  stream.flush();
  return static_cast<bool>(stream);
#else
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd < 0) return false;
  const char* data = content.data();
  std::size_t remaining = content.size();
  while (remaining > 0) {
    const ssize_t written = ::write(fd, data, remaining);
    if (written <= 0) {
      ::close(fd);
      return false;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
  ::fsync(fd);
  ::close(fd);
  return true;
#endif
}

bool write_keys(const std::map<std::string, std::string>& keys) {
  Json root = Json::object();
  for (const auto& entry : keys) root.set(entry.first, entry.second);

  std::error_code error;
  std::filesystem::create_directories(auth_directory(), error);
#ifndef _WIN32
  ::chmod(auth_directory().c_str(), S_IRWXU);  // 0700
#endif

  const std::string target = auth_file_path();
  const std::string temp = target + ".tmp";
  if (!write_private_file(temp, root.dump() + "\n")) {
    std::filesystem::remove(temp, error);
    return false;
  }

  // Atomic replace: a crash mid-write never leaves a truncated key file.
  std::filesystem::rename(temp, target, error);
  if (error) {
    std::filesystem::remove(temp, error);
    return false;
  }
#ifndef _WIN32
  ::chmod(target.c_str(), S_IRUSR | S_IWUSR);  // 0600
#endif
  return true;
}

}  // namespace

std::string auth_file_path() {
  return (std::filesystem::path(auth_directory()) / "auth.json").string();
}

std::map<std::string, std::string> load_auth() {
  std::map<std::string, std::string> keys;
  std::ifstream stream(auth_file_path());
  if (!stream) return keys;

  std::ostringstream text;
  text << stream.rdbuf();

  Json parsed = Json::parse(text.str());
  if (!parsed.is_object()) return keys;
  for (const auto& field : parsed.fields()) {
    if (field.second.is_string() && !field.second.as_string().empty()) {
      keys[field.first] = field.second.as_string();
    }
  }
  return keys;
}

bool save_auth_key(const std::string& provider, const std::string& api_key) {
  if (provider.empty() || api_key.empty()) return false;
  std::map<std::string, std::string> keys = load_auth();
  keys[provider] = api_key;
  return write_keys(keys);
}

bool remove_auth_key(const std::string& provider) {
  std::map<std::string, std::string> keys = load_auth();
  if (keys.erase(provider) == 0) return false;
  return write_keys(keys);
}

std::string stored_api_key(const std::string& provider) {
  if (provider.empty()) return "";
  const std::map<std::string, std::string> keys = load_auth();
  const auto it = keys.find(provider);
  return it == keys.end() ? std::string() : it->second;
}

}  // namespace karta
