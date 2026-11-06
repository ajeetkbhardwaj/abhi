// Karta — provider credentials saved by the TUI `/login` command.
//
// Keys live in `$HOME/.karta/auth.json` (directory 0700, file 0600 on POSIX,
// written atomically). Environment variables always win, so CI and
// shell-driven setups keep working. `/logout` removes the saved entry.
#pragma once

#include <map>
#include <string>

namespace karta {

std::string auth_file_path();

// provider id -> API key
std::map<std::string, std::string> load_auth();

bool save_auth_key(const std::string& provider, const std::string& api_key);
bool remove_auth_key(const std::string& provider);

// Empty when nothing is stored for the provider.
std::string stored_api_key(const std::string& provider);

}  // namespace karta
