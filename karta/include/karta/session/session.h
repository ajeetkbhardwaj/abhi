// Karta — JSONL session persistence (port of core/session-manager.ts).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "karta/core/json.h"

namespace karta {

struct SessionRecord {
  std::vector<Json> messages;  // replayed context (post-compaction messages only)
  std::string id;
  std::string path;
  std::string cwd;
  std::string name;
  std::string provider;
  std::string model;
  std::string summary;  // last compaction summary, if any
  bool compacted = false;
};

class Session {
 public:
  static std::unique_ptr<Session> create(const std::string& cwd, const std::string& directory,
                                         const std::string& name, std::string* error,
                                         const std::string& explicit_id = "",
                                         const std::string& parent_session = "");
  static std::unique_ptr<Session> load(const std::string& path, std::string* error);

  // Most recent session for `cwd` under `directory`, or "" when none exists.
  static std::string find_latest(const std::string& cwd, const std::string& directory);
  // Session file whose id contains `id`, or "" when none exists.
  static std::string find_by_id(const std::string& id, const std::string& directory);
  static std::string default_directory();
  static std::string directory_for_cwd(const std::string& cwd, const std::string& override_dir);

  void append_message(const Json& message);
  void append_model_change(const std::string& provider, const std::string& model);
  void append_session_info(const std::string& name);
  void append_compaction(const std::string& summary, long long tokens_before);

  const SessionRecord& record() const { return record_; }
  const std::string& path() const { return record_.path; }
  const std::string& id() const { return record_.id; }

 private:
  Session() = default;
  void append_entry(Json entry);
  void set_parent(const Json& entry);

  SessionRecord record_;
  std::string last_entry_id_;
  std::string created_at_;
};

}  // namespace karta
