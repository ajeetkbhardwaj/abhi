#include "karta/tui/loader.h"

namespace karta::tui {
namespace {
const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
}

Loader::Loader(const std::string& message) : Text("", Style::Status), message_(message) {
  const_cast<Loader*>(this)->start();
}

void Loader::start() { running_ = true; }
void Loader::stop() { running_ = false; }

void Loader::set_message(const std::string& message) { message_ = message; }

std::vector<std::string> Loader::render(size_t width) const {
  Loader* self = const_cast<Loader*>(this);
  std::string indicator = self->running_ ? frames[self->frame_++ % 10] : "•";
  self->set_text(indicator + " " + self->message_);
  return Text::render(width);
}

}  // namespace karta::tui
