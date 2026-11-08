#include "karta/session/session.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "karta/core/id.h"
#include "karta/core/platform.h"
#include "karta/ai/provider.h"

namespace fs = std::filesystem;

namespace karta {
namespace {

std::string random_id() {
  static std::mt19937_64 generator(std::random_device{}());
  static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string out;
  for (int i = 0; i < 12; ++i) out.push_back(alphabet[generator() % 36]);
  return out;
}

std::string sanitize(const std::string& value) {
  std::string out;
  for (const char c : value) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_';
    out.push_back(safe ? c : '-');
  }
  while (!out.empty() && out.front() == '-') out.erase(out.begin());
  if (out.empty()) out = "root";
  if (out.size() > 120) out.resize(120);
  return out;
}

Json session_header(const std::string& id, const std::string& cwd, const std::string& name) {
  Json header = Json::object();
  header.set("type", "session");
  header.set("version", 3);
  header.set("id", id);
  header.set("timestamp", iso8601_now());
  header.set("cwd", cwd);
  if (!name.empty()) {
    // pi records names as a session_info entry; keep the header minimal and do the same.
  }
  return header;
}

std::string timestamp_for_filename() {
  std::string stamp = iso8601_now();
  for (char& c : stamp) {
    if (c == ':' || c == '.' || c == 'T' || c == 'Z') c = '-';
  }
  return stamp;
}

bool write_header(const std::string& path, const Json& header, const std::string& parent,
                  std::string* error) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    if (error) *error = "cannot create session file: " + path;
    return false;
  }
  Json line = header;
  if (!parent.empty()) line.set("parentSession", parent);
  stream << line.dump() << "\n";
  if (!stream) {
    if (error) *error = "cannot write session header: " + path;
    return false;
  }
  return true;
}

std::string last_entry_id(const std::vector<Json>& entries) {
  if (entries.empty()) return "";
  return entries.back()["id"].as_string();
}

}  // namespace

std::string Session::default_directory() {
  return (fs::path(home_directory()) / ".karta" / "sessions").string();
}

std::string Session::directory_for_cwd(const std::string& cwd, const std::string& override_dir) {
  const fs::path base = override_dir.empty() ? fs::path(default_directory()) : fs::path(override_dir);
  std::error_code error;
  const fs::path absolute = fs::absolute(cwd, error);
  return (base / sanitize(absolute.string())).string();
}

std::unique_ptr<Session> Session::create(const std::string& cwd, const std::string& directory,
                                         const std::string& name, std::string* error,
                                         const std::string& explicit_id,
                                         const std::string& parent_session) {
  std::error_code fs_error;
  fs::create_directories(directory, fs_error);
  if (fs_error) {
    if (error) *error = "cannot create session directory: " + directory;
    return nullptr;
  }

  auto session = std::unique_ptr<Session>(new Session());
  session->record_.id = explicit_id.empty() ? karta::Id::session_id() : explicit_id;
  session->record_.cwd = cwd;
  session->record_.name = name;
  session->record_.path =
      (fs::path(directory) / (timestamp_for_filename() + "_" + session->record_.id + ".jsonl")).string();

  const Json header = session_header(session->record_.id, cwd, name);
  if (!write_header(session->record_.path, header, parent_session, error)) return nullptr;
  session->created_at_ = header["timestamp"].as_string();
  if (!name.empty()) session->append_session_info(name);
  return session;
}

std::unique_ptr<Session> Session::load(const std::string& path, std::string* error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    if (error) *error = "cannot open session: " + path;
    return nullptr;
  }

  auto session = std::unique_ptr<Session>(new Session());
  session->record_.path = path;
  std::vector<Json> entries;
  std::string line;
  bool header_seen = false;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    std::string parse_error;
    const Json entry = Json::parse(line, &parse_error);
    if (!parse_error.empty()) continue;

    if (entry["type"].as_string() == "session" && !header_seen) {
      header_seen = true;
      session->record_.id = entry["id"].as_string();
      session->record_.cwd = entry["cwd"].as_string();
      session->created_at_ = entry["timestamp"].as_string();
      continue;
    }
    entries.push_back(entry);

    const std::string type = entry["type"].as_string();
    if (type == "message") {
      const Json& message = entry["message"];
      if (entry.contains("compaction")) {
        // Not used; compaction is its own entry type.
      }
      session->record_.messages.push_back(message);
    } else if (type == "compaction") {
      session->record_.compacted = true;
      session->record_.summary = entry["summary"].as_string();
      session->record_.messages.clear();
      session->record_.messages.push_back(make_user_message(
          "[Context summary: the earlier conversation was compacted]\n\n" +
          session->record_.summary));
    } else if (type == "model_change") {
      session->record_.provider = entry["provider"].as_string();
      session->record_.model = entry["modelId"].as_string();
    } else if (type == "session_info") {
      session->record_.name = entry["name"].as_string();
    }
  }

  if (!header_seen) {
    if (error) *error = "not a karta session file: " + path;
    return nullptr;
  }
  session->last_entry_id_ = last_entry_id(entries);
  return session;
}

std::string Session::find_by_id(const std::string& id, const std::string& directory) {
  if (id.empty()) return "";
  std::error_code error;
  if (!fs::is_directory(directory, error)) return "";
  for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
    if (!it->is_regular_file(error)) continue;
    const std::string name = it->path().filename().string();
    if (name.size() > 6 && name.substr(name.size() - 6) == ".jsonl" && name.find(id) != std::string::npos) {
      return it->path().string();
    }
  }
  return "";
}

std::string Session::find_latest(const std::string& cwd, const std::string& directory) {
  std::error_code error;
  if (!fs::is_directory(directory, error)) return "";
  fs::path best;
  std::filesystem::file_time_type best_time{};
  for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
    if (!it->is_regular_file(error)) continue;
    if (it->path().extension() != ".jsonl") continue;
    std::ifstream stream(it->path());
    std::string line;
    if (!std::getline(stream, line)) continue;
    const Json header = Json::parse(line);
    if (header["cwd"].as_string() != cwd) continue;
    const auto time = it->last_write_time(error);
    if (error) continue;
    if (best.empty() || time > best_time) {
      best = it->path();
      best_time = time;
    }
  }
  return best.empty() ? "" : best.string();
}

void Session::set_parent(const Json& entry) {
  last_entry_id_ = entry["id"].as_string();
}

void Session::append_entry(Json entry) {
  entry.set("id", karta::Id::uuidv7());
  if (!last_entry_id_.empty()) entry.set("parentId", last_entry_id_);
  else entry.set("parentId", Json());
  entry.set("timestamp", iso8601_now());

  std::ofstream stream(record_.path, std::ios::binary | std::ios::app);
  if (!stream) return;
  stream << entry.dump() << "\n";
  stream.flush();
  set_parent(entry);
}

void Session::append_message(const Json& message) {
  Json entry = Json::object();
  entry.set("type", "message");
  entry.set("message", message);
  append_entry(entry);
  record_.messages.push_back(message);
}

void Session::append_model_change(const std::string& provider, const std::string& model) {
  Json entry = Json::object();
  entry.set("type", "model_change");
  entry.set("provider", provider);
  entry.set("modelId", model);
  append_entry(entry);
  record_.provider = provider;
  record_.model = model;
}

void Session::append_session_info(const std::string& name) {
  Json entry = Json::object();
  entry.set("type", "session_info");
  if (!name.empty()) entry.set("name", name);
  append_entry(entry);
  record_.name = name;
}

void Session::append_compaction(const std::string& summary, long long tokens_before) {
  Json entry = Json::object();
  entry.set("type", "compaction");
  entry.set("summary", summary);
  entry.set("tokensBefore", tokens_before);
  append_entry(entry);
  record_.compacted = true;
  record_.summary = summary;
}

}  // namespace karta
