// Karta — MCP (Model Context Protocol) client implementation.
// Full JSON-RPC 2.0 client with stdio/HTTP transports, tools, resources, prompts, roots, logging, sampling.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "karta/core/json.h"

namespace karta::mcp {

// ============================================================================
// JSON-RPC 2.0 Types
// ============================================================================

using JsonRpcId = std::variant<int64_t, std::string>;

struct JsonRpcRequest {
  std::string jsonrpc = "2.0";
  JsonRpcId id;
  std::string method;
  Json params;
};

struct JsonRpcResponse {
  std::string jsonrpc = "2.0";
  JsonRpcId id;
  Json result;
  struct Error {
    int code;
    std::string message;
    Json data;
  };
  std::optional<Error> error;
};

struct JsonRpcNotification {
  std::string jsonrpc = "2.0";
  std::string method;
  Json params;
};

using JsonRpcMessage = std::variant<JsonRpcRequest, JsonRpcResponse, JsonRpcNotification>;

// Standard JSON-RPC error codes
enum class JsonRpcErrorCode : int {
  ParseError = -32700,
  InvalidRequest = -32600,
  MethodNotFound = -32601,
  InvalidParams = -32602,
  InternalError = -32603,
  // MCP specific
  McpError = -32000,
  McpTimeout = -32001,
  McpAborted = -32002,
  McpConnectionClosed = -32003,
  McpAuthRequired = -32004,
  McpSessionExpired = -32005,
};

// ============================================================================
// MCP Protocol Types (mirroring pi-mcp)
// ============================================================================

struct Implementation {
  std::string name;
  std::string version;
  std::string title;
};

struct Root {
  std::string uri;
  std::string name;
};

struct ClientCapabilities {
  std::optional<std::map<std::string, Json>> experimental;
  std::optional<std::map<std::string, Json>> roots;
  std::optional<std::map<std::string, Json>> sampling;
  std::optional<std::map<std::string, Json>> elicitation;
};

struct ServerCapabilities {
  std::optional<std::map<std::string, Json>> experimental;
  std::optional<std::map<std::string, Json>> logging;
  std::optional<std::map<std::string, Json>> prompts;
  std::optional<std::map<std::string, Json>> resources;
  std::optional<std::map<std::string, Json>> tools;
  std::optional<std::map<std::string, Json>> completions;
};

struct InitializeParams {
  std::string protocolVersion;
  ClientCapabilities capabilities;
  Implementation clientInfo;
};

struct InitializeResult {
  std::string protocolVersion;
  ServerCapabilities capabilities;
  Implementation serverInfo;
  std::string instructions;
};

struct ProgressNotification {
  JsonRpcId progressToken;
  double progress;
  std::optional<double> total;
  std::optional<std::string> message;
};

struct CancelledNotification {
  JsonRpcId requestId;
  std::optional<std::string> reason;
};

struct ToolAnnotations {
  std::optional<std::string> title;
  std::optional<bool> readOnlyHint;
  std::optional<bool> destructiveHint;
  std::optional<bool> idempotentHint;
  std::optional<bool> openWorldHint;
};

struct Tool {
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  Json inputSchema;  // JSON Schema object
  std::optional<Json> outputSchema;
  std::optional<ToolAnnotations> annotations;
  std::optional<std::string> execution;  // "forbidden" | "optional" | "required"
};

struct ListToolsResult {
  std::vector<Tool> tools;
  std::optional<std::string> nextCursor;
};

struct CallToolParams {
  std::string name;
  Json arguments;
};

struct CallToolResult {
  std::vector<Json> content;  // TextContent | ImageContent | ResourceLink | Resource
  std::optional<bool> isError;
  std::optional<Json> structuredContent;
  std::optional<Json> _meta;
};

struct ContentAnnotations {
  std::optional<std::string> audience;
  std::optional<double> priority;
};

struct TextResourceContents {
  std::string uri;
  std::optional<std::string> mimeType;
  std::string text;
  std::optional<ContentAnnotations> annotations;
};

struct BlobResourceContents {
  std::string uri;
  std::optional<std::string> mimeType;
  std::string blob;  // base64
  std::optional<ContentAnnotations> annotations;
};

using ResourceContents = std::variant<TextResourceContents, BlobResourceContents>;

struct Resource {
  std::string uri;
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::string> mimeType;
  std::optional<int64_t> size;
  std::optional<ContentAnnotations> annotations;
};

struct ListResourcesResult {
  std::vector<Resource> resources;
  std::optional<std::string> nextCursor;
};

struct ResourceTemplate {
  std::string uriTemplate;
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::string> mimeType;
  std::optional<ContentAnnotations> annotations;
};

struct ListResourceTemplatesResult {
  std::vector<ResourceTemplate> resourceTemplates;
  std::optional<std::string> nextCursor;
};

struct ReadResourceParams {
  std::string uri;
};

struct ReadResourceResult {
  std::vector<ResourceContents> contents;
};

struct Prompt {
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<Json> arguments;  // JSON Schema for arguments
};

struct ListPromptsResult {
  std::vector<Prompt> prompts;
  std::optional<std::string> nextCursor;
};

struct PromptArgument {
  std::string name;
  std::optional<std::string> value;
};

struct GetPromptParams {
  std::string name;
  std::optional<std::map<std::string, std::string>> arguments;
};

struct PromptMessage {
  std::string role;  // "user" | "assistant"
  Json content;      // TextContent | ImageContent | ResourceLink
};

struct GetPromptResult {
  std::optional<std::string> description;
  std::vector<PromptMessage> messages;
};

struct LoggingParams {
  std::string level;  // "debug" | "info" | "notice" | "warning" | "error" | "critical" | "alert" | "emergency"
  std::optional<std::string> logger;
  Json data;
};

struct SamplingParams {
  Json messages;  // Array of message objects
  std::optional<Json> systemPrompt;
  std::optional<Json> toolDeclarations;
  std::optional<int> maxTokens;
  std::optional<double> temperature;
  std::optional<std::string> stopSequence;
};

struct SamplingResult {
  std::string role;  // "user" | "assistant"
  Json content;     // TextContent | ImageContent
  std::optional<std::string> stopReason;
};

// ============================================================================
// Transport Interface
// ============================================================================

class McpTransport {
 public:
  using MessageCallback = std::function<void(const JsonRpcMessage&)>;
  using ErrorCallback = std::function<void(const std::exception&)>;
  using CloseCallback = std::function<void()>;

  virtual ~McpTransport() = default;
  virtual void start() = 0;
  virtual void send(const JsonRpcMessage& message) = 0;
  virtual void close() = 0;
  virtual bool isStarted() const = 0;
  virtual bool isClosed() const = 0;

  // Event callbacks (set by client)
  MessageCallback onMessage;
  ErrorCallback onError;
  CloseCallback onClose;
};

// ============================================================================
// MCP Client
// ============================================================================

struct McpClientOptions {
  Implementation clientInfo;
  std::optional<ClientCapabilities> capabilities = {};
  std::optional<std::string> protocolVersion = "2025-11-25";
  std::optional<int> requestTimeoutMs = 30000;
  std::optional<std::vector<Root>> roots = {};
};

struct McpRequestOptions {
  std::optional<std::chrono::milliseconds> timeout;
  std::optional<std::function<void(const ProgressNotification&)>> onProgress;
  std::optional<std::shared_ptr<std::atomic<bool>>> cancelSignal;
};

class McpClient {
 public:
  enum class State { Idle, Connecting, Connected, Closed };

  explicit McpClient(McpClientOptions options);
  ~McpClient();

  State connectionState() const { return state_; }
  const InitializeResult& initializeResult() const { return initResult_; }
  const ServerCapabilities& serverCapabilities() const { return initResult_.capabilities; }
  const Implementation& serverInfo() const { return initResult_.serverInfo; }
  const std::string& instructions() const { return initResult_.instructions; }

  // Connect with a transport
  InitializeResult connect(std::unique_ptr<McpTransport> transport);

  // Request/response
  Json request(const std::string& method, const Json& params = Json(),
               const McpRequestOptions& options = {});

  // Notification (fire-and-forget)
  void notify(const std::string& method, const Json& params = Json());

  // Convenience methods
  std::vector<Tool> listTools(const McpRequestOptions& options = {});
  std::vector<Resource> listResources(const McpRequestOptions& options = {});
  std::vector<ResourceTemplate> listResourceTemplates(const McpRequestOptions& options = {});
  ReadResourceResult readResource(const std::string& uri, const McpRequestOptions& options = {});
  std::vector<Prompt> listPrompts(const McpRequestOptions& options = {});
  GetPromptResult getPrompt(const std::string& name,
                            const std::map<std::string, std::string>& arguments = {},
                            const McpRequestOptions& options = {});
  CallToolResult callTool(const std::string& name, const Json& arguments = Json(),
                          const McpRequestOptions& options = {});
  void ping(const McpRequestOptions& options = {});

  // Event handlers
  void onNotification(const std::string& method,
                      std::function<void(const Json&)> handler);
  void onError(std::function<void(const std::exception&)> handler);
  void onClose(std::function<void()> handler);

  // Cleanup
  void close();

 private:
  McpClientOptions options_;
  State state_ = State::Idle;
  std::unique_ptr<McpTransport> transport_;
  InitializeResult initResult_;
  std::string protocolVersion_;
  std::atomic<int64_t> nextRequestId_{1};
  std::mutex pendingMutex_;
  std::map<int64_t, std::pair<std::function<void(const Json&)>,
                              std::function<void(const std::exception&)>>> pendingRequests_;
  std::map<std::string, std::function<void(const Json&)>> notificationHandlers_;
  std::function<void(const std::exception&)> errorHandler_;
  std::function<void()> closeHandler_;

  void handleMessage(const JsonRpcMessage& message);
  void handleResponse(const JsonRpcResponse& response);
  void handleNotification(const JsonRpcNotification& notification);
  void handleError(const JsonRpcResponse::Error& error, int64_t id);
  void armTimeout(int64_t id, const McpRequestOptions& options);
  void cancelPending(int64_t id, const std::exception& error);
  void markClosed(const std::exception& error);
  void handleTransportClose();
};

// ============================================================================
// Transport Factories
// ============================================================================

struct StdioTransportOptions {
  std::string command;
  std::vector<std::string> args;
  std::string cwd;
  std::map<std::string, std::string> env;
  bool inheritEnv = true;
  std::string stderrMode = "pipe";  // "pipe" | "inherit"
  std::function<void(const std::string&)> onStderr;
  size_t maxMessageBytes = 10 * 1024 * 1024;
  size_t maxStderrBytes = 10 * 1024 * 1024;
  int closeTimeoutMs = 2000;
};

struct HttpTransportOptions {
  std::string url;
  std::map<std::string, std::string> headers;
  std::optional<std::function<std::string()>> authProvider;  // Returns bearer token
  size_t maxMessageBytes = 10 * 1024 * 1024;
};

std::unique_ptr<McpTransport> createStdioTransport(const StdioTransportOptions& options);
std::unique_ptr<McpTransport> createHttpTransport(const HttpTransportOptions& options);

// ============================================================================
// OAuth Support (for HTTP transports)
// ============================================================================

struct OAuthSettings {
  std::optional<std::string> clientId;
  std::optional<std::string> clientSecret;
  std::optional<int> callbackPort;
  std::optional<std::string> callbackUrl;
  std::optional<std::string> scope;
  std::optional<std::string> clientName;
  std::optional<std::string> clientRegistration;  // "auto" | "dcr" | "cimd"
  std::optional<std::string> authServerMetadataUrl;
};

struct OAuthChallenge {
  std::string resourceMetadataUrl;
  std::string scope;
};

class McpAuthProvider {
 public:
  virtual ~McpAuthProvider() = default;
  virtual std::string getToken() = 0;
  virtual void onChallenge(const OAuthChallenge& challenge) = 0;
  virtual void settled() = 0;
};

std::unique_ptr<McpAuthProvider> createMcpAuthProvider(
    const std::string& serverUrl,
    const std::string& credentialStorePath,
    const OAuthSettings& settings,
    std::function<void(const OAuthChallenge&)> onChallenge);

}  // namespace karta::mcp