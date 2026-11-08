// Karta — Model Context Protocol client (port of @earendil-works/pi-mcp).
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "json.h"

namespace karta {

// Forward declarations
class McpTransport;

// ------------------------------------------------------------------ Protocol types

struct McpImplementation {
  std::string name;
  std::string version;
  std::string title;  // optional
};

struct McpToolAnnotations {
  std::string title;
  bool readOnlyHint = false;
  bool destructiveHint = false;
  bool idempotentHint = false;
  bool openWorldHint = false;
};

struct McpTool {
  std::string name;
  std::string title;       // optional
  std::string description; // optional
  Json inputSchema;        // JSON Schema object
  McpToolAnnotations annotations;
};

struct McpCallToolResult {
  std::vector<Json> content;  // array of content blocks (type: "text" | "image" | ...)
  bool isError = false;
  Json structuredContent;     // optional
};

struct McpClientOptions {
  McpImplementation clientInfo;
  std::vector<std::string> roots;  // URIs
  int requestTimeoutMs = 30000;
};

// --------------------------------------------------------------- Transport interface

class McpTransport {
 public:
  virtual ~McpTransport() = default;
  virtual bool start() = 0;  // true on success
  virtual bool send(const Json& message) = 0;
  virtual void close() = 0;

  using MessageHandler = std::function<void(const Json&)>;
  using ErrorHandler = std::function<void(const std::string&)>;
  using CloseHandler = std::function<void()>;

  virtual void onMessage(MessageHandler handler) = 0;
  virtual void onError(ErrorHandler handler) = 0;
  virtual void onClose(CloseHandler handler) = 0;
};

// ----------------------------------------------------------------- Client core

class McpClient {
 public:
  explicit McpClient(McpClientOptions options);
  ~McpClient();

  // Connect using the given transport. Returns false on failure (check lastError).
  bool connect(std::unique_ptr<McpTransport> transport);

  // Disconnect and close the transport.
  void close();

  // Server capabilities after initialize.
  const McpImplementation& serverInfo() const { return serverInfo_; }
  const Json& serverCapabilities() const { return serverCapabilities_; }
  bool isConnected() const { return state_ == "connected"; }

  // List available tools (follows pagination).
  std::vector<McpTool> listTools(std::string* error = nullptr);

  // Call a tool by name with arguments.
  McpCallToolResult callTool(const std::string& name, const Json& arguments,
                             std::string* error = nullptr);

  // Last error message.
  const std::string& lastError() const { return lastError_; }

 private:
  struct PendingRequest {
    std::function<void(const Json&)> resolve;
    std::function<void(const std::string&)> reject;
  };

  bool request(const std::string& method, const Json& params, Json* result);
  void handleMessage(const Json& message);
  void handleRequest(const Json& request);
  void handleNotification(const std::string& method, const Json& params);

  // Resolve (or reject) the pending request with this id, if any. Returns true when found.
  bool settle_pending(int id, const Json* result, const std::string* error);

  McpClientOptions options_;
  std::unique_ptr<McpTransport> transport_;
  std::string state_ = "idle";  // idle | connecting | connected | closed
  std::atomic<int> nextRequestId_{1};
  McpImplementation serverInfo_;
  Json serverCapabilities_ = Json::object();
  std::string lastError_;
  std::mutex pending_mutex_;
  std::map<int, PendingRequest> pending_;
};

// ----------------------------------------------------------- Stdio transport (child process)

struct McpStdioTransportOptions {
  std::string command;
  std::vector<std::string> args;
  std::string cwd;
  std::map<std::string, std::string> env;  // extra env vars
  int maxMessageBytes = 1024 * 1024;       // 1MB
  int closeTimeoutMs = 2000;
};

class McpStdioTransport : public McpTransport {
 public:
  explicit McpStdioTransport(McpStdioTransportOptions options);
  ~McpStdioTransport() override;

  bool start() override;
  bool send(const Json& message) override;
  void close() override;

  void onMessage(MessageHandler handler) override { messageHandler_ = std::move(handler); }
  void onError(ErrorHandler handler) override { errorHandler_ = std::move(handler); }
  void onClose(CloseHandler handler) override { closeHandler_ = std::move(handler); }

 private:
  void readLoop();
  void processLine(const std::string& line);

  McpStdioTransportOptions options_;
  MessageHandler messageHandler_;
  ErrorHandler errorHandler_;
  CloseHandler closeHandler_;

#ifdef _WIN32
  void* child_ = nullptr;   // HANDLE
  void* stdinWrite_ = nullptr;
  void* stdoutRead_ = nullptr;
#else
  pid_t child_ = -1;
  int stdinWrite_ = -1;
  int stdoutRead_ = -1;
#endif
  bool started_ = false;
  bool closed_ = false;
  std::string stderrBuffer_;
};

// ------------------------------------------------------ Tool name helpers (naming.ts port)

// Creates `mcp__<server>__<tool>`, sanitized and shortened with hash suffix if too long.
std::string createMcpToolName(const std::string& server, const std::string& tool,
                              const std::function<bool(const std::string&)>& isTaken = nullptr);

// Server namespace prefix: `mcp__<server>` with `-` -> `_`.
std::string mcpNamespace(const std::string& server);

// Normalize input schema to an object schema with `properties`.
Json normalizeMcpInputSchema(const Json& schema);

// ------------------------------------------------ content conversion (protocol/content.ts port)

// Convert a tools/call result into text (+ note) for the model. Text and embedded text
// resources pass through; images/audio/resource-links/blobs become short placeholders;
// a result with no content blocks but with structuredContent becomes its JSON.
std::string mcpResultToText(const McpCallToolResult& result);

// Keep model-facing text within MCP_OUTPUT_MAX_BYTES by cutting its middle (Codex-style).
std::string limitMcpOutput(const std::string& text);

// The limit, matching pi-mcp's MCP_OUTPUT_MAX_BYTES.
constexpr std::size_t kMcpOutputMaxBytes = 20 * 1024;

}  // namespace karta
