// Karta — MCP client implementation (minimal working version)

#include "karta/mcp/mcp.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>

#include "karta/core/id.h"
#include "karta/core/platform.h"

namespace karta::mcp {

// ============================================================================
// JSON-RPC Helpers
// ============================================================================

static Json jsonRpcRequest(int64_t id, const std::string& method, const Json& params) {
  Json req = Json::object();
  req.set("jsonrpc", "2.0");
  req.set("id", id);
  req.set("method", method);
  if (!params.is_null()) req.set("params", params);
  return req;
}

static Json jsonRpcNotification(const std::string& method, const Json& params) {
  Json notif = Json::object();
  notif.set("jsonrpc", "2.0");
  notif.set("method", method);
  if (!params.is_null()) notif.set("params", params);
  return notif;
}

static bool isJsonRpcRequest(const Json& msg) {
  return msg["jsonrpc"].as_string() == "2.0" && (msg["id"].is_number() || msg["id"].is_string()) && msg["method"].is_string();
}

static bool isJsonRpcResponse(const Json& msg) {
  return msg["jsonrpc"].as_string() == "2.0" && (msg["id"].is_number() || msg["id"].is_string()) && !msg["method"].is_string();
}

static bool isJsonRpcNotification(const Json& msg) {
  return msg["jsonrpc"].as_string() == "2.0" && !msg["id"].is_number() && !msg["id"].is_string() && msg["method"].is_string();
}

static int64_t extractId(const Json& msg) {
  if (msg["id"].is_number()) return msg["id"].as_int();
  if (msg["id"].is_string()) return std::stoll(msg["id"].as_string());
  return 0;
}

// ============================================================================
// MCP Client Implementation
// ============================================================================

McpClient::McpClient(McpClientOptions options) : options_(std::move(options)) {}

McpClient::~McpClient() {
  if (state_ != State::Closed && transport_) {
    close();
  }
}

InitializeResult McpClient::connect(std::unique_ptr<McpTransport> transport) {
  if (state_ != State::Idle) {
    throw std::runtime_error("Cannot connect MCP client in non-idle state");
  }
  state_ = State::Connecting;
  transport_ = std::move(transport);

  // Set up transport event handlers
  transport_->onMessage = [this](const JsonRpcMessage& msg) { handleMessage(msg); };
  transport_->onError = [this](const std::exception& err) {
    if (errorHandler_) errorHandler_(err);
  };
  transport_->onClose = [this]() { handleTransportClose(); };

  transport_->start();

  // Send initialize request
  Json caps = Json::object();

  Json params = Json::object();
  params.set("protocolVersion", options_.protocolVersion.value_or("2025-11-25"));
  params.set("capabilities", caps);
  params.set("clientInfo", Json::object());
  params["clientInfo"].set("name", options_.clientInfo.name);
  params["clientInfo"].set("version", options_.clientInfo.version);
  if (!options_.clientInfo.title.empty()) {
    params["clientInfo"].set("title", options_.clientInfo.title);
  }

  Json result = request("initialize", params, {});

  // Parse initialize result
  initResult_.protocolVersion = result["protocolVersion"].as_string();
  initResult_.serverInfo.name = result["serverInfo"]["name"].as_string();
  initResult_.serverInfo.version = result["serverInfo"]["version"].as_string();
  initResult_.serverInfo.title = result["serverInfo"]["title"].as_string();
  initResult_.instructions = result["instructions"].as_string();
  protocolVersion_ = initResult_.protocolVersion;

  // Send initialized notification
  notify("notifications/initialized", Json());

  state_ = State::Connected;
  return initResult_;
}

Json McpClient::request(const std::string& method, const Json& params,
                        const McpRequestOptions& options) {
  if (state_ != State::Connected) {
    throw std::runtime_error("MCP client not connected");
  }

  int64_t id = nextRequestId_++;
  std::promise<Json> promise;
  auto future = promise.get_future();

  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingRequests_[id] = {
        [promise = std::move(promise)](const Json& result) mutable { promise.set_value(result); },
        [promise = std::promise<Json>()](const std::exception& err) mutable {
          promise.set_exception(std::make_exception_ptr(err));
        }};
  }

  // Arm timeout
  if (options.timeout.has_value() || options_.requestTimeoutMs.has_value()) {
    int timeoutMs = options.timeout.value_or(std::chrono::milliseconds(options_.requestTimeoutMs.value())).count();
    std::thread([this, id, timeoutMs]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
      std::lock_guard<std::mutex> lock(pendingMutex_);
      auto it = pendingRequests_.find(id);
      if (it != pendingRequests_.end()) {
        it->second.second(std::runtime_error("Request timeout"));
        pendingRequests_.erase(it);
      }
    }).detach();
  }

  // Send request
  Json req = jsonRpcRequest(id, method, params);
  transport_->send(req);

  // Wait for response
  try {
    return future.get();
  } catch (...) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingRequests_.erase(id);
    throw;
  }
}

void McpClient::notify(const std::string& method, const Json& params) {
  if (state_ != State::Connected) return;
  Json notif = jsonRpcNotification(method, params);
  transport_->send(notif);
}

void McpClient::handleMessage(const JsonRpcMessage& msg) {
  if (std::holds_alternative<JsonRpcResponse>(msg)) {
    handleResponse(std::get<JsonRpcResponse>(msg));
  } else if (std::holds_alternative<JsonRpcNotification>(msg)) {
    handleNotification(std::get<JsonRpcNotification>(msg));
  }
}

void McpClient::handleResponse(const JsonRpcResponse& response) {
  int64_t id = extractId(response);
  std::lock_guard<std::mutex> lock(pendingMutex_);
  auto it = pendingRequests_.find(id);
  if (it == pendingRequests_.end()) return;

  if (response.error.has_value()) {
    it->second.second(std::runtime_error(response.error->message));
  } else {
    it->second.first(response.result);
  }
  pendingRequests_.erase(it);
}

void McpClient::handleNotification(const JsonRpcNotification& notification) {
  auto it = notificationHandlers_.find(notification.method);
  if (it != notificationHandlers_.end()) {
    it->second(notification.params);
  }
}

void McpClient::onNotification(const std::string& method,
                               std::function<void(const Json&)> handler) {
  notificationHandlers_[method] = std::move(handler);
}

void McpClient::onError(std::function<void(const std::exception&)> handler) {
  errorHandler_ = std::move(handler);
}

void McpClient::onClose(std::function<void()> handler) {
  closeHandler_ = std::move(handler);
}

void McpClient::close() {
  if (state_ == State::Closed) return;
  state_ = State::Closed;

  // Fail all pending requests
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    for (auto& [id, handlers] : pendingRequests_) {
      handlers.second(std::runtime_error("Connection closed"));
    }
    pendingRequests_.clear();
  }

  if (transport_) {
    transport_->close();
    transport_.reset();
  }

  if (closeHandler_) closeHandler_();
}

std::vector<Tool> McpClient::listTools(const McpRequestOptions& options) {
  Json result = request("tools/list", Json(), options);
  std::vector<Tool> tools;
  for (const auto& toolJson : result["tools"].items()) {
    Tool tool;
    tool.name = toolJson["name"].as_string();
    tool.title = toolJson["title"].as_string();
    tool.description = toolJson["description"].as_string();
    tool.inputSchema = toolJson["inputSchema"];
    tool.outputSchema = toolJson["outputSchema"];
    if (toolJson["annotations"].is_object()) {
      ToolAnnotations ann;
      ann.title = toolJson["annotations"]["title"].as_string();
      ann.readOnlyHint = toolJson["annotations"]["readOnlyHint"].as_bool();
      ann.destructiveHint = toolJson["annotations"]["destructiveHint"].as_bool();
      ann.idempotentHint = toolJson["annotations"]["idempotentHint"].as_bool();
      ann.openWorldHint = toolJson["annotations"]["openWorldHint"].as_bool();
      tool.annotations = ann;
    }
    tools.push_back(std::move(tool));
  }
  return tools;
}

std::vector<Resource> McpClient::listResources(const McpRequestOptions& options) {
  Json result = request("resources/list", Json(), options);
  std::vector<Resource> resources;
  for (const auto& r : result["resources"].items()) {
    Resource res;
    res.uri = r["uri"].as_string();
    res.name = r["name"].as_string();
    res.title = r["title"].as_string();
    res.description = r["description"].as_string();
    res.mimeType = r["mimeType"].as_string();
    res.size = r["size"].as_int();
    resources.push_back(std::move(res));
  }
  return resources;
}

std::vector<ResourceTemplate> McpClient::listResourceTemplates(const McpRequestOptions& options) {
  Json result = request("resources/templates/list", Json(), options);
  std::vector<ResourceTemplate> templates;
  for (const auto& t : result["resourceTemplates"].items()) {
    ResourceTemplate tmpl;
    tmpl.uriTemplate = t["uriTemplate"].as_string();
    tmpl.name = t["name"].as_string();
    tmpl.title = t["title"].as_string();
    tmpl.description = t["description"].as_string();
    tmpl.mimeType = t["mimeType"].as_string();
    templates.push_back(std::move(tmpl));
  }
  return templates;
}

ReadResourceResult McpClient::readResource(const std::string& uri,
                                           const McpRequestOptions& options) {
  Json params = Json::object();
  params.set("uri", uri);
  Json result = request("resources/read", params, options);
  ReadResourceResult rr;
  for (const auto& c : result["contents"].items()) {
    if (c["text"].is_string()) {
      TextResourceContents tc;
      tc.uri = c["uri"].as_string();
      tc.mimeType = c["mimeType"].as_string();
      tc.text = c["text"].as_string();
      rr.contents.push_back(tc);
    } else if (c["blob"].is_string()) {
      BlobResourceContents bc;
      bc.uri = c["uri"].as_string();
      bc.mimeType = c["mimeType"].as_string();
      bc.blob = c["blob"].as_string();
      rr.contents.push_back(bc);
    }
  }
  return rr;
}

std::vector<Prompt> McpClient::listPrompts(const McpRequestOptions& options) {
  Json result = request("prompts/list", Json(), options);
  std::vector<Prompt> prompts;
  for (const auto& p : result["prompts"].items()) {
    Prompt pmt;
    pmt.name = p["name"].as_string();
    pmt.title = p["title"].as_string();
    pmt.description = p["description"].as_string();
    pmt.arguments = p["arguments"];
    prompts.push_back(std::move(pmt));
  }
  return prompts;
}

GetPromptResult McpClient::getPrompt(const std::string& name,
                                     const std::map<std::string, std::string>& arguments,
                                     const McpRequestOptions& options) {
  Json params = Json::object();
  params.set("name", name);
  if (!arguments.empty()) {
    Json args = Json::object();
    for (const auto& [k, v] : arguments) args.set(k, v);
    params.set("arguments", args);
  }
  Json result = request("prompts/get", params, options);
  GetPromptResult gpr;
  gpr.description = result["description"].as_string();
  for (const auto& m : result["messages"].items()) {
    PromptMessage pm;
    pm.role = m["role"].as_string();
    pm.content = m["content"];
    gpr.messages.push_back(std::move(pm));
  }
  return gpr;
}

CallToolResult McpClient::callTool(const std::string& name, const Json& arguments,
                                   const McpRequestOptions& options) {
  Json params = Json::object();
  params.set("name", name);
  if (!arguments.is_null()) params.set("arguments", arguments);
  Json result = request("tools/call", params, options);
  CallToolResult ctr;
  for (const auto& c : result["content"].items()) {
    ctr.content.push_back(c);
  }
  ctr.isError = result["isError"].as_bool();
  ctr.structuredContent = result["structuredContent"];
  ctr._meta = result["_meta"];
  return ctr;
}

void McpClient::ping(const McpRequestOptions& options) {
  request("ping", Json(), options);
}

void McpClient::onNotification(const std::string& method,
                               std::function<void(const Json&)> handler) {
  notificationHandlers_[method] = std::move(handler);
}

void McpClient::onError(std::function<void(const std::exception&)> handler) {
  errorHandler_ = std::move(handler);
}

void McpClient::onClose(std::function<void()> handler) {
  closeHandler_ = std::move(handler);
}

void McpClient::handleMessage(const JsonRpcMessage& msg) {
  if (std::holds_alternative<JsonRpcResponse>(msg)) {
    handleResponse(std::get<JsonRpcResponse>(msg));
  } else if (std::holds_alternative<JsonRpcNotification>(msg)) {
    handleNotification(std::get<JsonRpcNotification>(msg));
  }
}

void McpClient::handleResponse(const JsonRpcResponse& response) {
  int64_t id = extractId(response);
  std::lock_guard<std::mutex> lock(pendingMutex_);
  auto it = pendingRequests_.find(id);
  if (it == pendingRequests_.end()) return;

  if (response.error.has_value()) {
    it->second.second(std::runtime_error(response.error->message));
  } else {
    it->second.first(response.result);
  }
  pendingRequests_.erase(it);
}

void McpClient::handleNotification(const JsonRpcNotification& notification) {
  auto it = notificationHandlers_.find(notification.method);
  if (it != notificationHandlers_.end()) {
    it->second(notification.params);
  }
}

void McpClient::onNotification(const std::string& method,
                               std::function<void(const Json&)> handler) {
  notificationHandlers_[method] = std::move(handler);
}

void McpClient::onError(std::function<void(const std::exception&)> handler) {
  errorHandler_ = std::move(handler);
}

void McpClient::onClose(std::function<void()> handler) {
  closeHandler_ = std::move(handler);
}

void McpClient::handleTransportClose() {
  if (state_ != State::Closed) {
    markClosed(std::runtime_error("Transport closed"));
  }
}

void McpClient::markClosed(const std::exception& error) {
  if (state_ == State::Closed) return;
  state_ = State::Closed;

  std::lock_guard<std::mutex> lock(pendingMutex_);
  for (auto& [id, handlers] : pendingRequests_) {
    handlers.second(error);
  }
  pendingRequests_.clear();

  if (closeHandler_) closeHandler_();
}

}  // namespace karta::mcp