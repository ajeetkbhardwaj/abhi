// Karta — Model Context Protocol client (port of @earendil-works/pi-mcp).
#include "mcp.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace karta {
namespace {

constexpr const char* kLatestProtocolVersion = "2025-11-25";

bool supported_protocol_version(const std::string& version) {
  return version == "2025-11-25" || version == "2025-06-18" || version == "2025-03-26" ||
         version == "2024-11-05";
}

bool is_json_rpc_response(const Json& message) {
  return message.is_object() && message["jsonrpc"].is_string() && message.contains("id") &&
         (message.contains("result") || message.contains("error"));
}

bool is_json_rpc_request(const Json& message) {
  return message.is_object() && message["jsonrpc"].is_string() && message.contains("id") &&
         message.contains("method") && !message.contains("result") && !message.contains("error");
}

bool is_json_rpc_notification(const Json& message) {
  return message.is_object() && message["jsonrpc"].is_string() && !message.contains("id") &&
         message.contains("method");
}

// Simple FNV-1a hash for the tool-name shortening (sha256 is overkill; same shape).
std::string short_hash(const std::string& input, int length) {
  unsigned long long value = 1469598103934665603ULL;
  for (const char c : input) {
    value ^= static_cast<unsigned char>(c);
    value *= 1099511628211ULL;
  }
  const char* hex = "0123456789abcdef";
  std::string out;
  for (int i = 0; i < length; ++i) {
    out.push_back(hex[value & 0xf]);
    value >>= 4;
    if (value == 0) {
      // Continue with the next slice of the input so long names stay distinct.
      unsigned long long next = 1469598103934665603ULL;
      for (const char c : input) {
        next ^= static_cast<unsigned char>(c) + static_cast<unsigned char>(i);
        next *= 1099511628211ULL;
      }
      value = next;
    }
  }
  return out;
}

}  // namespace

// ------------------------------------------------------------------ Client core

McpClient::McpClient(McpClientOptions options) : options_(std::move(options)) {}

McpClient::~McpClient() { close(); }

bool McpClient::connect(std::unique_ptr<McpTransport> transport) {
  lastError_.clear();
  state_ = "connecting";
  transport_ = std::move(transport);

  std::mutex response_mutex;
  bool got_response = false;

  transport_->onMessage([&](const Json& message) {
    if (is_json_rpc_response(message)) {
      std::lock_guard<std::mutex> lock(response_mutex);
      got_response = true;
      handleMessage(message);
    } else {
      handleMessage(message);
    }
  });
  transport_->onError([&](const std::string& error) { lastError_ = error; });
  transport_->onClose([&] {
    if (state_ == "connected" || state_ == "connecting") state_ = "closed";
  });

  if (!transport_->start()) {
    close();
    if (lastError_.empty()) lastError_ = "failed to start MCP server";
    return false;
  }

  // initialize request
  Json params = Json::object();
  params.set("protocolVersion", kLatestProtocolVersion);
  Json capabilities = Json::object();
  if (!options_.roots.empty()) {
    Json roots = Json::object();
    capabilities.set("roots", roots);
  }
  params.set("capabilities", capabilities);
  Json client_info = Json::object();
  client_info.set("name", options_.clientInfo.name);
  client_info.set("version", options_.clientInfo.version);
  if (!options_.clientInfo.title.empty()) client_info.set("title", options_.clientInfo.title);
  params.set("clientInfo", client_info);

  Json result;
  if (!request("initialize", params, &result)) {
    close();
    return false;
  }
  if (!result.is_object() || result["protocolVersion"].as_string().empty() ||
      !result["serverInfo"].is_object()) {
    lastError_ = "Invalid MCP initialize result";
    close();
    return false;
  }
  const std::string version = result["protocolVersion"].as_string();
  if (!supported_protocol_version(version)) {
    lastError_ = "MCP server selected unsupported protocol version " + version;
    close();
    return false;
  }
  serverInfo_.name = result["serverInfo"]["name"].as_string();
  serverInfo_.version = result["serverInfo"]["version"].as_string();
  serverCapabilities_ = result["capabilities"];

  // notifications/initialized
  Json notification = Json::object();
  notification.set("jsonrpc", "2.0");
  notification.set("method", "notifications/initialized");
  if (!transport_->send(notification)) {
    lastError_ = "failed to send notifications/initialized";
    close();
    return false;
  }

  state_ = "connected";
  return true;
}

void McpClient::close() {
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto& [id, handler] : pending_) {
      handler.reject("MCP client closed");
    }
    pending_.clear();
  }
  if (transport_) {
    transport_->close();
    transport_.reset();
  }
  if (state_ != "closed") state_ = "closed";
}

bool McpClient::request(const std::string& method, const Json& params, Json* result) {
  if (!transport_ || (state_ != "connected" && state_ != "connecting")) {
    lastError_ = "MCP client is " + state_;
    return false;
  }
  const int id = nextRequestId_++;
  Json message = Json::object();
  message.set("jsonrpc", "2.0");
  message.set("id", id);
  message.set("method", method);
  if (!params.is_null()) message.set("params", params);

  std::mutex response_mutex;
  std::condition_variable condition;
  bool done = false;
  Json response;
  std::string error;

  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_[id] = {
        [&](const Json& r) {
          std::lock_guard<std::mutex> lock(response_mutex);
          response = r;
          done = true;
          condition.notify_one();
        },
        [&](const std::string& e) {
          std::lock_guard<std::mutex> lock(response_mutex);
          error = e;
          done = true;
          condition.notify_one();
        }};
  }

  if (!transport_->send(message)) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.erase(id);
    lastError_ = "failed to send " + method;
    return false;
  }

  const auto timeout = std::chrono::milliseconds(options_.requestTimeoutMs);
  {
    std::unique_lock<std::mutex> lock(response_mutex);
    if (!condition.wait_for(lock, timeout, [&] { return done; })) {
      std::lock_guard<std::mutex> pending_lock(pending_mutex_);
      pending_.erase(id);
      lastError_ = method + " timed out after " + std::to_string(options_.requestTimeoutMs) + "ms";
      return false;
    }
  }

  if (!error.empty()) {
    lastError_ = error;
    return false;
  }
  if (result) *result = response;
  return true;
}

bool McpClient::settle_pending(int id, const Json* result, const std::string* error) {
  PendingRequest handler;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    const auto it = pending_.find(id);
    if (it == pending_.end()) return false;
    handler = std::move(it->second);
    pending_.erase(it);
  }
  if (result) handler.resolve(*result);
  if (error) handler.reject(*error);
  return true;
}

void McpClient::handleMessage(const Json& message) {
  if (is_json_rpc_response(message)) {
    const Json& id = message["id"];
    if (!id.is_number()) return;
    if (message.contains("error")) {
      const Json& err = message["error"];
      const std::string error_text = "MCP error " + std::to_string(err["code"].as_int(0)) + ": " +
                                     err["message"].as_string();
      settle_pending(static_cast<int>(id.as_int()), nullptr, &error_text);
    } else {
      settle_pending(static_cast<int>(id.as_int()), &message["result"], nullptr);
    }
  } else if (is_json_rpc_request(message)) {
    handleRequest(message);
  } else if (is_json_rpc_notification(message)) {
    handleNotification(message["method"].as_string(), message["params"]);
  }
}

void McpClient::handleRequest(const Json& request) {
  if (!transport_) return;
  const std::string method = request["method"].as_string();
  const Json& id = request["id"];
  Json response = Json::object();
  response.set("jsonrpc", "2.0");
  response.set("id", id);

  if (method == "ping") {
    response.set("result", Json::object());
  } else if (method == "roots/list") {
    Json roots = Json::array();
    for (const std::string& uri : options_.roots) {
      Json root = Json::object();
      root.set("uri", uri);
      roots.push_back(root);
    }
    Json result = Json::object();
    result.set("roots", roots);
    response.set("result", result);
  } else {
    Json error = Json::object();
    error.set("code", -32601);
    error.set("message", "Method not found: " + method);
    response.set("error", error);
  }
  transport_->send(response);
}

void McpClient::handleNotification(const std::string& method, const Json&) {
  (void)method;
  // notifications/progress, notifications/cancelled — not used in the core flow.
}

std::vector<McpTool> McpClient::listTools(std::string* error) {
  std::vector<McpTool> tools;
  std::string cursor;
  bool has_cursor = false;
  int pages = 0;
  const int kMaxPages = 1000;

  while (pages < kMaxPages) {
    Json params = Json::object();
    if (has_cursor) params.set("cursor", cursor);
    Json result;
    if (!request("tools/list", params, &result)) {
      if (error) *error = lastError_;
      return {};
    }
    const Json& items = result["tools"];
    if (!items.is_array()) {
      if (error) *error = "Invalid MCP tools/list result";
      return {};
    }
    for (const Json& item : items.items()) {
      if (!item.is_object() || item["name"].as_string().empty() || !item["inputSchema"].is_object()) {
        if (error) *error = "Invalid entry in MCP tools/list result";
        return {};
      }
      McpTool tool;
      tool.name = item["name"].as_string();
      tool.title = item["title"].as_string();
      tool.description = item["description"].as_string();
      tool.inputSchema = item["inputSchema"];
      if (item.contains("annotations")) {
        const Json& annotations = item["annotations"];
        tool.annotations.title = annotations["title"].as_string();
        tool.annotations.readOnlyHint = annotations["readOnlyHint"].as_bool(false);
        tool.annotations.destructiveHint = annotations["destructiveHint"].as_bool(false);
        tool.annotations.idempotentHint = annotations["idempotentHint"].as_bool(false);
        tool.annotations.openWorldHint = annotations["openWorldHint"].as_bool(false);
      }
      tools.push_back(std::move(tool));
    }
    const Json& next_cursor = result["nextCursor"];
    if (!next_cursor.is_string() || next_cursor.as_string().empty()) break;
    if (next_cursor.as_string() == cursor) {
      if (error) *error = "MCP tools/list returned duplicate cursor";
      return {};
    }
    cursor = next_cursor.as_string();
    has_cursor = true;
    pages++;
  }
  return tools;
}

McpCallToolResult McpClient::callTool(const std::string& name, const Json& arguments,
                                      std::string* error) {
  Json params = Json::object();
  params.set("name", name);
  if (!arguments.is_null()) params.set("arguments", arguments);
  Json result;
  if (!request("tools/call", params, &result)) {
    if (error) *error = lastError_;
    return {};
  }
  McpCallToolResult out;
  if (!result.is_object()) {
    if (error) *error = "Invalid MCP tools/call result";
    return {};
  }
  if (result.contains("content")) {
    const Json& content = result["content"];
    if (content.is_array()) {
      for (const Json& block : content.items()) out.content.push_back(block);
    }
  }
  out.isError = result["isError"].as_bool(false);
  if (result.contains("structuredContent")) out.structuredContent = result["structuredContent"];
  return out;
}

// ------------------------------------------------------------------ Stdio transport

McpStdioTransport::McpStdioTransport(McpStdioTransportOptions options)
    : options_(std::move(options)) {}

McpStdioTransport::~McpStdioTransport() { close(); }

bool McpStdioTransport::start() {
  if (started_) return false;
  started_ = true;

  std::string command_line = options_.command;
  for (const std::string& arg : options_.args) {
    command_line += " ";
    if (arg.find_first_of(" \t\"'\\") != std::string::npos) {
      command_line += "\"";
      for (const char c : arg) {
        if (c == '"' || c == '\\') command_line.push_back('\\');
        command_line.push_back(c);
      }
      command_line += "\"";
    } else {
      command_line += arg;
    }
  }

#ifdef _WIN32
  // Windows: CreateProcess with pipes.
  SECURITY_ATTRIBUTES attrs{};
  attrs.nLength = sizeof(attrs);
  attrs.bInheritHandle = TRUE;
  HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
  if (!CreatePipe(&in_read, &in_write, &attrs, 0) || !CreatePipe(&out_read, &out_write, &attrs, 0)) {
    errorHandler_("failed to create pipes");
    return false;
  }
  SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = in_read;
  startup.hStdOutput = out_write;
  // stderr goes to our stderr: it must not share the MCP stdio framing pipe.
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION process{};
  std::wstring wide_cmd;
  for (const char c : command_line) wide_cmd.push_back(static_cast<wchar_t>(c));
  wide_cmd.push_back(L'\0');

  if (!CreateProcessW(nullptr, wide_cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                      nullptr, &startup, &process)) {
    CloseHandle(in_read);
    CloseHandle(in_write);
    CloseHandle(out_read);
    CloseHandle(out_write);
    errorHandler_("failed to start " + options_.command);
    return false;
  }
  CloseHandle(in_read);
  CloseHandle(out_write);
  child_ = process.hProcess;
  stdinWrite_ = in_write;
  stdoutRead_ = out_read;
  // Start a read thread.
  std::thread([this]() { readLoop(); }).detach();
  return true;
#else
  int in_pipe[2], out_pipe[2];
  if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
    errorHandler_("failed to create pipes");
    return false;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    errorHandler_("failed to fork");
    return false;
  }
  if (pid == 0) {
    // Child.
    dup2(in_pipe[0], STDIN_FILENO);
    dup2(out_pipe[1], STDOUT_FILENO);
    // stderr goes to our stderr: it must not share the MCP stdio framing pipe.
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    if (!options_.cwd.empty()) {
      if (chdir(options_.cwd.c_str()) != 0) _exit(126);
    }
    // Extra env vars.
    for (const auto& kv : options_.env) {
      setenv(kv.first.c_str(), kv.second.c_str(), 1);
    }
    // Run the command via shell.
    execl("/bin/sh", "sh", "-c", command_line.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  // Parent.
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  child_ = pid;
  stdinWrite_ = in_pipe[1];
  stdoutRead_ = out_pipe[0];
  // Non-blocking read.
  const int flags = fcntl(stdoutRead_, F_GETFL, 0);
  fcntl(stdoutRead_, F_SETFL, flags | O_NONBLOCK);

  // Start a read thread.
  std::thread([this]() { readLoop(); }).detach();
  return true;
#endif
}

void McpStdioTransport::readLoop() {
  std::string buffer;
  char chunk[8192];
  while (!closed_) {
    ssize_t count;
#ifdef _WIN32
    DWORD available = 0;
    if (!PeekNamedPipe((HANDLE)stdoutRead_, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
      Sleep(10);
      continue;
    }
    count = static_cast<ssize_t>(available);
    if (count > static_cast<ssize_t>(sizeof(chunk))) count = sizeof(chunk);
    DWORD bytes_read = 0;
    if (!ReadFile((HANDLE)stdoutRead_, chunk, static_cast<DWORD>(count), &bytes_read, nullptr)) {
      break;
    }
    count = static_cast<ssize_t>(bytes_read);
#else
    pollfd descriptor{};
    descriptor.fd = stdoutRead_;
    descriptor.events = POLLIN;
    const int ready = poll(&descriptor, 1, 100);
    if (ready < 0) break;
    if (ready == 0) continue;
    count = ::read(stdoutRead_, chunk, sizeof(chunk));
    if (count <= 0) {
      if (count == 0) break;
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
      break;
    }
#endif
    buffer.append(chunk, static_cast<std::size_t>(count));
    // Process complete lines.
    std::size_t pos;
    while ((pos = buffer.find('\n')) != std::string::npos) {
      std::string line = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      // Strip trailing \r.
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) processLine(line);
    }
    if (buffer.size() > static_cast<std::size_t>(options_.maxMessageBytes)) {
      buffer.clear();
      if (errorHandler_) errorHandler_("MCP message exceeded max size");
    }
  }
  // Process any remaining buffer.
  if (!buffer.empty()) processLine(buffer);
  buffer.clear();
  if (closeHandler_) closeHandler_();
}

void McpStdioTransport::processLine(const std::string& line) {
  if (line.empty()) return;
  std::string parse_error;
  const Json message = Json::parse(line, &parse_error);
  if (!parse_error.empty()) {
    if (errorHandler_) errorHandler_("invalid JSON from MCP server: " + parse_error);
    return;
  }
  if (messageHandler_) messageHandler_(message);
}

bool McpStdioTransport::send(const Json& message) {
  if (!started_ || closed_ || !stdinWrite_) return false;
  const std::string payload = message.dump() + "\n";
#ifdef _WIN32
  DWORD written = 0;
  if (!WriteFile((HANDLE)stdinWrite_, payload.data(), static_cast<DWORD>(payload.size()), &written,
                 nullptr)) {
    return false;
  }
  return written == payload.size();
#else
  const ssize_t written = ::write(stdinWrite_, payload.data(), payload.size());
  return written == static_cast<ssize_t>(payload.size());
#endif
}

void McpStdioTransport::close() {
  if (closed_) return;
  closed_ = true;
#ifdef _WIN32
  if (stdinWrite_) {
    CloseHandle((HANDLE)stdinWrite_);
    stdinWrite_ = nullptr;
  }
  if (stdoutRead_) {
    CloseHandle((HANDLE)stdoutRead_);
    stdoutRead_ = nullptr;
  }
  if (child_) {
    TerminateProcess((HANDLE)child_, 1);
    CloseHandle((HANDLE)child_);
    child_ = nullptr;
  }
#else
  if (stdinWrite_ >= 0) {
    ::close(stdinWrite_);
    stdinWrite_ = -1;
  }
  if (stdoutRead_ >= 0) {
    ::close(stdoutRead_);
    stdoutRead_ = -1;
  }
  if (child_ > 0) {
    // Graceful shutdown: close stdin, wait briefly, then SIGTERM, then SIGKILL.
    kill(child_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
      const pid_t result = waitpid(child_, &status, WNOHANG);
      if (result == child_ || result < 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    // Check if it's still alive.
    if (waitpid(child_, &status, WNOHANG) == 0) {
      kill(child_, SIGKILL);
      waitpid(child_, &status, 0);
    }
    child_ = -1;
  }
#endif
}

// ------------------------------------------------------------------ Naming helpers

std::string createMcpToolName(const std::string& server, const std::string& tool,
                              const std::function<bool(const std::string&)>& isTaken) {
  std::string name = "mcp__" + server + "__" + tool;
  for (char& c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
      c = '_';
    }
  }
  constexpr std::size_t kMaxLength = 64;
  if (name.size() <= kMaxLength && (!isTaken || !isTaken(name))) return name;
  const std::string hash = short_hash(server + "\0" + tool, 8);
  return name.substr(0, kMaxLength - hash.size() - 1) + "_" + hash;
}

std::string mcpNamespace(const std::string& server) {
  std::string name = server;
  for (char& c : name) {
    if (c == '-') c = '_';
  }
  return "mcp__" + name;
}

Json normalizeMcpInputSchema(const Json& schema) {
  Json normalized = schema;
  if (!normalized.contains("type")) normalized.set("type", "object");
  if (!normalized.contains("properties")) normalized.set("properties", Json::object());
  return normalized;
}

// ---------------------------------------------------------------- content conversion

std::string mcpResultToText(const McpCallToolResult& result) {
  std::string text;
  auto append_block = [&](const Json& block) {
    if (!block.is_object()) return;
    const std::string type = block["type"].as_string();
    if (type == "text") {
      text += block["text"].as_string();
    } else if (type == "image") {
      text += "[image " + block["mimeType"].as_string() + " omitted]";
    } else if (type == "audio") {
      text += "[audio " + block["mimeType"].as_string() + " omitted]";
    } else if (type == "resource_link") {
      text += block["name"].as_string() + ": " + block["uri"].as_string();
    } else if (type == "resource") {
      const Json& resource = block["resource"];
      if (resource.contains("text")) {
        text += resource["text"].as_string();
      } else if (resource.contains("blob")) {
        const std::string mime = resource.contains("mimeType") ? resource["mimeType"].as_string() : "unknown";
        if (mime.rfind("image/", 0) == 0) {
          text += "[image " + mime + " omitted]";
        } else {
          text += "[binary resource " + resource["uri"].as_string() + " (" + mime + ") omitted]";
        }
      }
    } else {
      text += "[unsupported MCP content " + type + "]";
    }
  };

  for (const Json& block : result.content) {
    if (!text.empty()) text += "\n";
    append_block(block);
  }
  if (text.empty() && !result.structuredContent.is_null()) {
    text = result.structuredContent.dump();
  }
  return text;
}

std::string limitMcpOutput(const std::string& text) {
  if (text.size() <= kMcpOutputMaxBytes) return text;

  // Cut at character boundaries only: a byte whose top two bits are 10 is a UTF-8 continuation.
  const auto is_boundary = [&text](std::size_t index) {
    return index >= text.size() || (static_cast<unsigned char>(text[index]) & 0xc0) != 0x80;
  };
  const std::size_t half = kMcpOutputMaxBytes / 2;
  std::size_t head_end = half;
  while (head_end > 0 && !is_boundary(head_end)) --head_end;
  std::size_t tail_start = text.size() - (kMcpOutputMaxBytes - half);
  while (tail_start < text.size() && !is_boundary(tail_start)) ++tail_start;

  // Count removed characters (UTF-8 decode length of the middle slice).
  std::size_t removed_chars = 0;
  for (std::size_t i = head_end; i < tail_start; ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) ++removed_chars;
  }

  long long total_lines = 1;
  for (const char c : text) {
    if (c == '\n') ++total_lines;
  }
  const long long tokens = static_cast<long long>((text.size() + 3) / 4);

  std::ostringstream out;
  out << "Warning: truncated output (original token count: " << tokens << ")\n"
      << "Total output lines: " << total_lines << "\n\n"
      << text.substr(0, head_end) << "…" << removed_chars << " chars truncated…"
      << text.substr(tail_start);
  return out.str();
}

}  // namespace karta
