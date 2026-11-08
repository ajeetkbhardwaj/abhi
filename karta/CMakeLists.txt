cmake_minimum_required(VERSION 3.16)
project(karta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(KARTA_BUILD_TUI "Build the terminal UI (ncurses)" OFF)

# Categorized subproject sources (canonical). Public headers: include/karta/<category>/.
add_executable(karta
  src/main.cpp
  src/agent/agent.cpp
  src/agent/prompt.cpp
  src/ai/http.cpp
  src/ai/provider.cpp
  src/cli/cli.cpp
  src/core/auth.cpp
  src/core/id.cpp
  src/core/json.cpp
  src/core/platform.cpp
  src/core/telemetry.cpp
  src/mcp/mcp.cpp
  src/session/session.cpp
  src/tools/tools.cpp
)

target_compile_options(karta PRIVATE -Wall -Wextra -Wno-unused-parameter)
# Headers from include/ (karta/...). src/ retained for local/private resolution only.
target_include_directories(karta PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(karta PRIVATE CURL::libcurl Threads::Threads)

if(KARTA_BUILD_TUI)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(NCURSES ncursesw QUIET)
  if(NOT NCURSES_FOUND)
    pkg_check_modules(NCURSES ncurses REQUIRED)
  endif()

  target_compile_definitions(karta PRIVATE KARTA_TUI)
  target_include_directories(karta PRIVATE ${NCURSES_INCLUDE_DIRS})
  target_link_libraries(karta PRIVATE ${NCURSES_LIBRARIES})
  target_sources(karta PRIVATE
    src/tui/screen.cpp
    src/tui/app.cpp
    src/tui/model-picker.cpp
    src/tui/select-list.cpp
    src/tui/text.cpp
    src/tui/loader.cpp
    src/tui/scroll-view.cpp
    src/tui/editor.cpp
  )
  message(STATUS "TUI enabled - `karta` opens the terminal UI when attached to a TTY")
else()
  message(STATUS "TUI disabled - pass -DKARTA_BUILD_TUI=ON to build the terminal UI")
endif()

install(TARGETS karta RUNTIME DESTINATION bin)
