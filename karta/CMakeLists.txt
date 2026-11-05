cmake_minimum_required(VERSION 3.16)
project(karta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optional TUI toolkit (ncurses)
# Set KARTA_BUILD_TUI=ON to enable the terminal UI submodule
option(KARTA_BUILD_TUI "Build the terminal UI submodule" OFF)

add_executable(karta
  src/main.cpp
  src/json.cpp
  src/platform.cpp
  src/http.cpp
  src/provider.cpp
  src/tools.cpp
  src/agent.cpp
  src/session.cpp
  src/prompt.cpp
  src/cli.cpp
)

# Core compile options
target_compile_options(karta PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_include_directories(karta PRIVATE src)

# Conditional TUI source and ncurses link
if(KARTA_BUILD_TUI)
  find_package(NCurses REQUIRED)
  target_include_directories(karta PRIVATE src)
  target_link_libraries(karta PRIVATE CURL::libcurl Threads::Threads NCurses::NCursesNCurses)
  
  # TUI source files
  target_sources(karta PRIVATE
    src/tui.cpp
    src/tui.h
  )
  
  install(TARGETS karta RUNTIME DESTINATION bin)
  # Also install the TUI shared concept (no binary, just headers)
else()
  install(TARGETS karta RUNTIME DESTINATION bin)
endif()
