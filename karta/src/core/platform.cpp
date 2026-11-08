#include "karta/core/platform.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <random>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace karta {
namespace {

// Keeps the head of the stream in memory and spills the remainder to a file,
// mirroring pi's head truncation with a full-output escape hatch.
class OutputCollector {
 public:
  OutputCollector(std::size_t cap, std::string* head) : cap_(cap), head_(head) {}

  void add(const char* data, std::size_t length) {
    if (!truncated_ && head_->size() + length <= cap_) {
      head_->append(data, length);
      return;
    }
    if (!truncated_) {
      truncated_ = true;
      full_path_ = temp_path();
      full_ = std::fopen(full_path_.c_str(), "wb");
      if (full_) {
        std::fwrite(head_->data(), 1, head_->size(), full_);
        head_->append(data, length < cap_ ? length : cap_);
        // Only keep the first cap_ bytes of the head.
        if (head_->size() > cap_) head_->resize(cap_);
        std::fwrite(data, 1, length, full_);
        return;
      }
      // Could not open a spill file: keep truncating in memory only.
    }
    if (full_) std::fwrite(data, 1, length, full_);
    if (head_->size() < cap_) {
      const std::size_t room = cap_ - head_->size();
      head_->append(data, length < room ? length : room);
    }
  }

  void finish() {
    if (full_) {
      std::fclose(full_);
      full_ = nullptr;
    }
  }

  bool truncated() const { return truncated_; }
  const std::string& full_path() const { return full_path_; }

 private:
  static std::string temp_path() {
    std::error_code error;
    std::filesystem::path dir = std::filesystem::temp_directory_path(error);
    if (error) dir = ".";
    std::random_device device;
    std::mt19937 generator(device());
    std::ostringstream name;
    name << "karta-" << epoch_millis() << "-" << (generator() % 100000) << ".out";
    return (dir / name.str()).string();
  }

  std::size_t cap_;
  std::string* head_;
  bool truncated_ = false;
  std::string full_path_;
  std::FILE* full_ = nullptr;
};

}  // namespace

std::string home_directory() {
#ifdef _WIN32
  const char* value = std::getenv("USERPROFILE");
  if (!value || !*value) value = std::getenv("HOMEDRIVE");
#else
  const char* value = std::getenv("HOME");
#endif
  if (value && *value) return value;
  std::error_code error;
  return std::filesystem::current_path(error).string();
}

std::string current_working_directory() {
  std::error_code error;
  const auto path = std::filesystem::current_path(error);
  return error ? std::string(".") : path.string();
}

std::string iso8601_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900,
                utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                static_cast<int>(millis));
  return buffer;
}

long long epoch_millis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string os_description() {
#ifdef _WIN32
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unix";
#endif
}

void enable_utf8_console() {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
}

bool stdout_is_tty() {
#ifdef _WIN32
  return _isatty(_fileno(stdout)) != 0;
#else
  return isatty(STDOUT_FILENO) != 0;
#endif
}

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& text) {
  if (text.empty()) return std::wstring();
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

}  // namespace

ProcessResult run_shell(const std::string& command, const std::string& cwd, int timeout_seconds,
                        std::size_t max_bytes) {
  ProcessResult result;
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;

  HANDLE read_handle = nullptr;
  HANDLE write_handle = nullptr;
  if (!CreatePipe(&read_handle, &write_handle, &attributes, 0)) {
    result.output = "failed to create pipe";
    return result;
  }
  SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_handle;
  startup.hStdError = write_handle;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION process{};
  std::wstring command_line = L"cmd.exe /d /s /c \"" + widen(command) + L"\"";
  std::wstring directory = widen(cwd);
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');

  const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process);
  CloseHandle(write_handle);
  if (!created) {
    CloseHandle(read_handle);
    result.output = "failed to start process";
    return result;
  }

  OutputCollector collector(max_bytes, &result.output);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  bool running = true;
  while (running) {
    if (WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0) running = false;
    char buffer[8192];
    DWORD available = 0;
    while (PeekNamedPipe(read_handle, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
      DWORD read_bytes = 0;
      if (!ReadFile(read_handle, buffer, sizeof(buffer), &read_bytes, nullptr) || read_bytes == 0) break;
      collector.add(buffer, read_bytes);
    }
    if (running && timeout_seconds > 0 && std::chrono::steady_clock::now() > deadline) {
      TerminateProcess(process.hProcess, 1);
      result.timed_out = true;
      running = false;
    }
  }

  char buffer[8192];
  DWORD read_bytes = 0;
  while (ReadFile(read_handle, buffer, sizeof(buffer), &read_bytes, nullptr) && read_bytes > 0) {
    collector.add(buffer, read_bytes);
  }
  collector.finish();

  DWORD exit_code = 0;
  GetExitCodeProcess(process.hProcess, &exit_code);
  result.exit_code = result.timed_out ? 124 : static_cast<int>(exit_code);
  result.truncated = collector.truncated();
  result.full_output_path = collector.full_path();

  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(read_handle);
  return result;
}

#else

ProcessResult run_shell(const std::string& command, const std::string& cwd, int timeout_seconds,
                        std::size_t max_bytes) {
  ProcessResult result;

  int fds[2];
  if (pipe(fds) != 0) {
    result.output = "failed to create pipe";
    return result;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    result.output = "failed to fork";
    return result;
  }

  if (pid == 0) {
    // Child: own process group so a timeout can reap the whole tree.
    setpgid(0, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]);
    close(fds[1]);
    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) _exit(126);
    }
    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }

  setpgid(pid, pid);
  close(fds[1]);
  const int flags = fcntl(fds[0], F_GETFL, 0);
  fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

  OutputCollector collector(max_bytes, &result.output);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  bool timed_out = false;
  bool eof = false;

  while (!eof) {
    pollfd descriptor{};
    descriptor.fd = fds[0];
    descriptor.events = POLLIN;
    const int ready = poll(&descriptor, 1, 50);
    if (ready > 0) {
      char buffer[8192];
      while (true) {
        const ssize_t count = read(fds[0], buffer, sizeof(buffer));
        if (count > 0) {
          collector.add(buffer, static_cast<std::size_t>(count));
          continue;
        }
        if (count == 0) eof = true;
        break;
      }
    }
    if (!eof && timeout_seconds > 0 && std::chrono::steady_clock::now() > deadline) {
      kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
      timed_out = true;
      break;
    }
  }

  if (timed_out) {
    // Drain whatever the killed process left in the pipe.
    char buffer[8192];
    while (read(fds[0], buffer, sizeof(buffer)) > 0) {
    }
  }
  close(fds[0]);
  collector.finish();

  int status = 0;
  waitpid(pid, &status, 0);
  if (timed_out) {
    result.timed_out = true;
    result.exit_code = 124;
  } else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  result.truncated = collector.truncated();
  result.full_output_path = collector.full_path();
  return result;
}

#endif

}  // namespace karta
