// Karta — MCP stdio transport (spawns child process)

#include "karta/mcp/mcp.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace karta::mcp {

namespace {

class LineReader {
 public:
  using MessageCallback = std::function<void(const std::string&)>;
  using ErrorCallback = std::function<void(const std::string&)>;

  LineReader(size_t maxMessageBytes, size_t maxStderrBytes,
             MessageCallback onMessage, ErrorCallback onError,
             ErrorCallback onStderr)
      : maxMessageBytes_(maxMessageBytes),
        maxStderrBytes_(maxStderrBytes),
        onMessage_(std::move(onMessage)),
        onError_(std::move(onError)),
        onStderr_(std::move(onStderr)) {}

  void feedStdout(const char* data, size_t len) {
    stdoutBuffer_.append(data, len);
    processBuffer(stdoutBuffer_, [this](const std::string& line) {
      onMessage_(line);
    });
  }

  void feedStderr(const char* data, size_t len) {
    stderrBuffer_.append(data, len);
    processBuffer(stderrBuffer_, [this](const std::string& line) {
      if (onStderr_) onStderr_(line);
    });
  }

  void end() {
    if (!stdoutBuffer_.empty()) {
      try {
        onMessage_(stdoutBuffer_);
      } catch (...) {
        // Ignore parse errors on partial messages
      }
    }
    if (!stderrBuffer_.empty() && onStderr_) {
      onStderr_(stderrBuffer_);
    }
  }

  const std::string& stderr() const { return stderrBuffer_; }

 private:
  void processBuffer(std::string& buffer, std::function<void(const std::string&)> handler) {
    size_t pos = 0;
    while ((pos = buffer.find('\n')) != std::string::npos) {
      std::string line = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) {
        handler(line);
      }
    }
  }

  size_t maxMessageBytes_;
  size_t maxStderrBytes_;
  std::string stdoutBuffer_;
  std::string stderrBuffer_;
  MessageCallback onMessage_;
  ErrorCallback onError_;
  ErrorCallback onStderr_;
};

}  // namespace

class StdioTransportImpl : public McpTransport {
 public:
  explicit StdioTransportImpl(StdioTransportOptions options)
      : options_(std::move(options)), childPid_(-1), stdinFd_(-1), stdoutFd_(-1), stderrFd_(-1) {}

  ~StdioTransportImpl() override {
    if (childPid_ > 0) {
      close();
    }
  }

  void start() override {
    if (started_) throw std::runtime_error("Transport already started");
    if (closed_) throw std::runtime_error("Transport closed");

    started_ = true;

    // Prepare environment
    std::vector<std::string> envVec;
    if (options_.inheritEnv) {
      for (char** env = environ; *env; ++env) {
        envVec.push_back(*env);
      }
    }
    for (const auto& [k, v] : options_.env) {
      envVec.push_back(k + "=" + v);
    }

    // Create pipes
    int stdinPipe[2], stdoutPipe[2], stderrPipe[2];
    if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0 || pipe(stderrPipe) < 0) {
      throw std::runtime_error("Failed to create pipes");
    }

    childPid_ = fork();
    if (childPid_ < 0) {
      close(stdinPipe[0]); close(stdinPipe[1]);
      close(stdoutPipe[0]); close(stdoutPipe[1]);
      close(stderrPipe[0]); close(stderrPipe[1]);
      throw std::runtime_error("Failed to fork process");
    }

    if (childPid_ == 0) {
      // Child process
      dup2(stdinPipe[0], STDIN_FILENO);
      dup2(stdoutPipe[1], STDOUT_FILENO);
      dup2(stderrPipe[1], STDERR_FILENO);

      close(stdinPipe[0]); close(stdinPipe[1]);
      close(stdoutPipe[0]); close(stdoutPipe[1]);
      close(stderrPipe[0]); close(stderrPipe[1]);

      if (!options_.cwd.empty()) {
        chdir(options_.cwd.c_str());
      }

      std::vector<char*> argv;
      argv.push_back(const_cast<char*>(options_.command.c_str()));
      for (const auto& arg : options_.args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);

      std::vector<char*> envp;
      for (const auto& e : envVec) {
        envp.push_back(const_cast<char*>(e.c_str()));
      }
      envp.push_back(nullptr);

      execvpe(argv[0], argv.data(), envp.data());
      _exit(127);
    }

    // Parent process
    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    close(stderrPipe[1]);

    stdinFd_ = stdinPipe[1];
    stdoutFd_ = stdoutPipe[0];
    stderrFd_ = stderrPipe[0];

    // Set non-blocking
    fcntl(stdoutFd_, F_SETFL, O_NONBLOCK);
    fcntl(stderrFd_, F_SETFL, O_NONBLOCK);

    // Start reader threads
    readerThread_ = std::thread(&StdioTransportImpl::readerLoop, this);
    stderrThread_ = std::thread(&StdioTransportImpl::stderrLoop, this);
  }

  void send(const JsonRpcMessage& message) override {
    if (!started_ || closed_) {
      throw std::runtime_error("Transport not started or closed");
    }

    Json json;
    if (std::holds_alternative<JsonRpcRequest>(message)) {
      const auto& req = std::get<JsonRpcRequest>(message);
      json = jsonRpcRequest(req.id, req.method, req.params);
    } else if (std::holds_alternative<JsonRpcNotification>(message)) {
      const auto& notif = std::get<JsonRpcNotification>(message);
      json = jsonRpcNotification(notif.method, notif.params);
    } else {
      throw std::runtime_error("Cannot send response via transport");
    }

    std::string payload = json.dump() + "\n";
    ssize_t written = write(stdinFd_, payload.c_str(), payload.size());
    if (written < 0) {
      throw std::runtime_error("Failed to write to stdin: " + std::string(strerror(errno)));
    }
  }

  void close() override {
    if (closed_) return;
    closed_ = true;

    // Close stdin to signal EOF
    if (stdinFd_ >= 0) {
      ::close(stdinFd_);
      stdinFd_ = -1;
    }

    // Wait for process to exit
    if (childPid_ > 0) {
      int status;
      int timeoutMs = options_.closeTimeoutMs;
      auto start = std::chrono::steady_clock::now();

      while (true) {
        int status;
        pid_t result = waitpid(childPid_, &status, WNOHANG);
        if (result == childPid_) {
          break;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed.count() >= timeoutMs) {
          kill(childPid_, SIGKILL);
          waitpid(childPid_, &status, 0);
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      childPid_ = -1;
    }

    if (stdoutFd_ >= 0) {
      ::close(stdoutFd_);
      stdoutFd_ = -1;
    }
    if (stderrFd_ >= 0) {
      ::close(stderrFd_);
      stderrFd_ = -1;
    }

    if (readerThread_.joinable()) readerThread_.join();
    if (stderrThread_.joinable()) stderrThread_.join();

    if (onClose) onClose();
  }

  bool isStarted() const override { return started_; }
  bool isClosed() const override { return closed_; }

  int pid() const { return childPid_; }
  const std::string& stderr() const { return stderrBuffer_; }

 private:
  void readerLoop() {
    char buffer[4096];
    while (!closed_) {
      ssize_t n = read(stdoutFd_, buffer, sizeof(buffer));
      if (n <= 0) break;
      lineReader_.feedStdout(buffer, n);
    }
  }

  void stderrLoop() {
    char buffer[4096];
    while (!closed_) {
      ssize_t n = read(stderrFd_, buffer, sizeof(buffer));
      if (n <= 0) break;
      lineReader_.feedStderr(buffer, n);
      std::lock_guard<std::mutex> lock(stderrMutex_);
      stderrBuffer_.append(buffer, n);
    }
  }

  StdioTransportOptions options_;
  pid_t childPid_;
  int stdinFd_;
  int stdoutFd_;
  int stderrFd_;
  bool started_ = false;
  bool closed_ = false;
  LineReader lineReader_{
      options_.maxMessageBytes,
      options_.maxStderrBytes,
      [this](const std::string& line) {
        try {
          Json json = Json::parse(line);
          JsonRpcMessage msg;
          if ((json["id"].is_number() || json["id"].is_string()) && json["method"].is_string()) {
            msg = JsonRpcRequest{
                .jsonrpc = json["jsonrpc"].as_string(),
                .id = json["id"].is_number() ? JsonRpcId(json["id"].as_int()) : JsonRpcId(json["id"].as_string()),
                .method = json["method"].as_string(),
                .params = json["params"]
            };
          } else {
            std::optional<JsonRpcResponse::Error> err;
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
          }
          if (onMessage) onMessage(msg);
        } catch (...) {
          // Ignore parse errors
        }
      },
      [this](const std::string& err) {
        if (onError) onError(std::runtime_error(err));
      },
      [this](const std::string& line) {
        if (options_.onStderr) options_.onStderr(line);
      }
  },
  std::thread readerThread_,
  std::thread stderrThread_,
  std::string stderrBuffer_,
  std::mutex stderrMutex_;
};

}  // namespace

std::unique_ptr<McpTransport> createStdioTransport(const StdioTransportOptions& options) {
  return std::make_unique<StdioTransportImpl>(options);
}

}  // namespace karta::mcp