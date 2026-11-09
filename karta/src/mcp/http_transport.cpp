// Karta — MCP HTTP transport (simplified - request/response only)

#include "karta/mcp/mcp.h"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>

#include "karta/core/id.h"

namespace karta::mcp {

namespace {

struct HttpState {
  std::string buffer;
  std::function<void(const JsonRpcMessage&)> onMessage;
  std::function<void(const std::exception&)> onError;
  std::function<void()> onClose;
  std::atomic<bool> cancelled{false};
};

class HttpTransportImpl : public McpTransport {
 public:
  explicit HttpTransportImpl(HttpTransportOptions options)
      : options_(std::move(options)), sessionId_() {}

  ~HttpTransportImpl() override { close(); }

  void start() override {
    if (started_) throw std::runtime_error("Transport already started");
    if (closed_) throw std::runtime_error("Transport closed");

    started_ = true;
    // HTTP transport uses request/response only, no persistent connection
  }

  void send(const JsonRpcMessage& message) override {
    if (!started_ || closed_) {
      throw std::runtime_error("Transport not started or closed");
    }

    Json json;
    if (std::holds_alternative<JsonRpcRequest>(message)) {
      json = std::get<JsonRpcRequest>(message);
    } else if (std::holds_alternative<JsonRpcNotification>(message)) {
      json = std::get<JsonRpcNotification>(message);
    } else {
      throw std::runtime_error("Cannot send response via transport");
    }

    sendPost(json);
  }

  void close() override {
    if (closed_) return;
    closed_ = true;

    if (onClose) onClose();
  }

  bool isStarted() const override { return started_; }
  bool isClosed() const override { return closed_; }

  void setProtocolVersion(const std::string& version) {
    protocolVersion_ = version;
  }

  const std::string& sessionId() const { return sessionId_; }

 private:
  void sendPost(const Json& json) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to initialize libcurl");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!protocolVersion_.empty()) {
      headers = curl_slist_append(headers, ("MCP-Protocol-Version: " + protocolVersion_).c_str());
    }
    if (!sessionId_.empty()) {
      headers = curl_slist_append(headers, ("Mcp-Session-Id: " + sessionId_).c_str());
    }
    for (const auto& [k, v] : options_.headers) {
      headers = curl_slist_append(headers, (k + ": " + v).c_str());
    }
    if (options_.authProvider) {
      std::string token = options_.authProvider();
      if (!token.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
      }
    }

    std::string payload = json.dump();
    std::string responseBody;

    curl_easy_setopt(curl, CURLOPT_URL, options_.url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* ptr, size_t size, size_t nmemb, void* userdata) {
      std::string* body = static_cast<std::string*>(userdata);
      body->append(ptr, size * nmemb);
      return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, [](char* buffer, size_t size, size_t nitems, void* userdata) {
      std::string header(buffer, size * nitems);
      if (header.rfind("mcp-session-id:", 0) == 0) {
        std::string* sessionId = static_cast<std::string*>(userdata);
        *sessionId = header.substr(14);
        // Trim whitespace
        size_t start = sessionId->find_first_not_of(" \t\r\n");
        size_t end = sessionId->find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
          *sessionId = sessionId->substr(start, end - start + 1);
        } else {
          sessionId->clear();
        }
      }
      return size * nitems;
    });
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &sessionId_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
      throw std::runtime_error("HTTP POST failed: " + std::string(curl_easy_strerror(res)));
    }

    // Parse response
    if (!responseBody.empty()) {
      try {
        Json json = Json::parse(responseBody);
        JsonRpcMessage msg;
        if (isJsonRpcRequest(json) || isJsonRpcResponse(json) || isJsonRpcNotification(json)) {
          if ((json["id"].is_number() || json["id"].is_string()) && json["method"].is_string()) {
            // Request - shouldn't happen in response
          } else if (json["id"].is_number() || json["id"].is_string()) {
            // Response
            JsonRpcResponse::Error err;
            if (json["error"].is_object()) {
              err = JsonRpcResponse::Error{
                  .code = json["error"]["code"].as_int(),
                  .message = json["error"]["message"].as_string(),
                  .data = json["error"]["data"]
              };
            }
            msg = JsonRpcResponse{
                .jsonrpc = json["jsonrpc"].as_string(),
                .id = json["id"].is_number() ? JsonRpcId(json["id"].as_int()) : JsonRpcId(json["id"].as_string()),
                .result = json["result"],
                .error = json["error"].is_object() ? std::optional<JsonRpcResponse::Error>(
                    JsonRpcResponse::Error{
                        .code = json["error"]["code"].as_int(),
                        .message = json["error"]["message"].as_string(),
                        .data = json["error"]["data"]
                    }) : std::nullopt
            };
            if (onMessage) onMessage(msg);
          } else {
            // Notification
            msg = JsonRpcNotification{
                .jsonrpc = json["jsonrpc"].as_string(),
                .method = json["method"].as_string(),
                .params = json["params"]
            };
            if (onMessage) onMessage(msg);
          }
        }
      } catch (...) {
        // Ignore parse errors
      }
    }
  }

  HttpTransportOptions options_;
  std::string protocolVersion_;
  std::string sessionId_;
  bool started_ = false;
  bool closed_ = false;
};

}  // namespace

std::unique_ptr<McpTransport> createHttpTransport(const HttpTransportOptions& options) {
  return std::make_unique<HttpTransportImpl>(options);
}

}  // namespace karta::mcp