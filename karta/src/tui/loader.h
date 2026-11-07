// Karta — Loader component (mirrors pi packages/tui/src/components/loader.ts).
#pragma once

#include <string>
#include <vector>

#include "tui/text.h"

namespace karta::tui {

class Loader : public Text {
 public:
  Loader(const std::string& message = "Loading...");
  void start();
  void stop();
  void set_message(const std::string& message);
  std::vector<std::string> render(size_t width) const;

 private:
  std::string message_;
  size_t frame_ = 0;
  bool running_ = false;
};

}  // namespace karta::tui
