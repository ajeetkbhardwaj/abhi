cmake_minimum_required(VERSION 3.16)
project(karta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optional terminal UI (ncurses). When enabled, launching `karta` with no
# prompt opens the TUI; /login and /model live there.
option(KARTA_BUILD_TUI "Build the terminal UI (ncurses)" OFF)

add_executable(karta
  src/main.cpp
  src/json.cpp
  src/platform.cpp
  src/http.cpp
  src/provider.cpp
  src/auth.cpp
  src/tools.cpp
  src/agent.cpp
  src/session.cpp
  src/prompt.cpp
  src/cli.cpp
  src/telemetry.cpp
  src/mcp.cpp
)

target_compile_options(karta PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_include_directories(karta PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(karta PRIVATE CURL::libcurl Threads::Threads)

if(KARTA_BUILD_TUI)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(NCURSES ncurses REQUIRED)

  target_compile_definitions(karta PRIVATE KARTA_TUI)
  target_include_directories(karta PRIVATE ${NCURSES_INCLUDE_DIRS})
  target_link_libraries(karta PRIVATE ${NCURSES_LIBRARIES})
  target_sources(karta PRIVATE    src/tui/screen.cpp
  src/tui/screen.h
  src/tui/app.cpp
  src/tui/app.h
  src/tui/model-picker.cpp
  src/tui/model-picker.h
  src/tui/select-list.cpp
  src/tui/select-list.h
  )
  message(STATUS "TUI enabled - `karta` opens the terminal UI when attached to a TTY")
else()
  message(STATUS "TUI disabled - pass -DKARTA_BUILD_TUI=ON to build the terminal UI")
endif()

install(TARGETS karta RUNTIME DESTINATION bin)
  target_sources(karta PRIVATE
    src/tui/text.cpp
    src/tui/text.h
    src/tui/loader.cpp
    src/tui/loader.h
    src/tui/scroll-view.cpp
    src/tui/scroll-view.h
    src/tui/editor.cpp
    src/tui/editor.h
  )
