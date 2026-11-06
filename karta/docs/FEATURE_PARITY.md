# Karta Feature Parity Matrix

This document catalogs the status of each feature from the original `pi`/pi-agent project in the Karta C++17 port.

## Ported (✅)

### packages/agent — Agent Core
- ✅ Agent loop with steering/follow-up queues
- ✅ Tool batches (ordered, unique, non-overlapping oldText)
- ✅ `length` truncation guard (2000 lines / 50KB)
- ✅ Event system (message_start/end, tool_execution_start/end, turn_start/end, agent_start/end)
- ✅ Compaction when message history exceeds threshold
- ✅ Tool-result messages emit `message_start/end` for persistence
- ✅ Session resume (`-c`, `--session-id`)
- ✅ Fork session (`--fork <path>`)
- ✅ Agents.md / Agents.override.md / CLAUDE.md discovery upward (max 3 files, 16KB each)

### packages/ai — LLM Provider Bridge
- ✅ OpenAI-compatible SSE streaming parser
- ✅ Anthropic-style provider support
- ✅ NVIDIA provider (`nvidia`, endpoint `https://integrate.api.nvidia.com/v1`, env `NVIDIA_API_KEY`)
- ✅ Model listing (`--list-models`)
- ✅ JSON mode events (emits `{"type":"message_end","message":{...}}`)
- ✅ Function/call tool calling via streaming

### packages/coding-agent — Tools
- ✅ `bash` (timeout max 2147483s, `/bin/sh -c` POSIX or `cmd.exe /d /s /c` Windows)
- ✅ `read` (offset/limit, 2000 lines / 50KB cap, offset 0 defaults to start)
- ✅ `write` (create file or append)
- ✅ `edit` (unique non-overlapping oldText → replacement)
- ✅ `grep` (regex / literal / glob / context / limit 100)
- ✅ `find` (glob, limit 1000)
- ✅ `ls` (500 entries max)
- ✅ `powershell` on Windows abstraction in platform.cpp

### packages/tui — Terminal UI
- ✅ ncurses UI in `karta/src/tui/` (`screen.{h,cpp}`, `app.{h,cpp}`), built with `-DKARTA_BUILD_TUI=ON`
- ✅ Transcript with wrapping, scrollback, streaming assistant text, tool lines
- ✅ Single-line editor: UTF-8 cursor movement, history, masked secret input
- ✅ Status-line spinner and header (provider, model, cwd, session id)
- ✅ `/login`, `/logout`, `/model` provider connection flow with saved credentials
- ✅ Agent and model-fetch workers with cancellation (Esc aborts HTTP)
- ❌ Markdown rendering, images, mouse, clipboard, autocomplete, themes, kill-ring/undo
- See `karta/docs/TUI.md` for the pi-component mapping

### Core infrastructure
- ✅ JSON parser/serializer (UTF-8 escapes, integer-preserving dump) in `karta/src/json.{h,cpp}`
- ✅ libcurl POST streaming with cancel via XFERINFO, 64MB body cap in `karta/src/http.{h,cpp}`
- ✅ Platform abstraction (`platform.cpp`): POSIX `fork/pipe/kill`, Win32 `CreateProcess`
- ✅ `std::filesystem` usage throughout
- ✅ Hand-written JSON parser (no external JSON dependency)
- ✅ Session format JSONL v3 (`type`, `version`, `id`, `timestamp`, `cwd`)
- ✅ `find_latest` and `find_by_id` for session resume
- ✅ REPL, RPC reader thread, SIGINT handling in CLI
- ✅ `--provider`, `--model`, `--mode`, `-p`, `-c`, `--session*`, `--max-turns`, `--compact-threshold`
- ✅ Environment variable driven API keys (never hardcoded)
- ✅ `.gitignore` support — `karta/api.txt` excluded from version control

## Not ported (❌) — documented as out of scope

| Package | Status | Notes |
|---------|--------|-------|
| `packages/mcp` | ❌ | Multi-Console-Protocol handshake not ported. |
| `packages/codemode` | ❌ | Not ported. |
| `packages/durable` | ❌ | Not ported. |
| `packages/chord` | ❌ | Not ported. |
| `packages/telemetry` | ❌ | Not ported. |
| `packages/evals` | ❌ | Not ported. |
| `apps/p2` | ❌ | Application-level flows not ported. |
| Extensions / skills | ❌ | Not ported. |
| Themes | ❌ | Not ported. |

## Known gaps / unsupported features (from original pi)

- MCP handshake and message persistence across connections
- TUI color themes, markdown rendering, images, mouse, clipboard, kill-ring/undo
- Line REPL (non-TTY fallback) color themes
- Durable session checkpointing beyond JSONL v3
- Chord protocol / peer-to-peer features
- Evals / structured test-harness orchestration
- Code-mode editor integration
- Skills system (per-package custom tools)
- Telemetry / tracing beyond console

## Build & run — Linux/macOS/Windows

### Linux/macOS

```bash
cd karta
cmake -S . -B build
cmake --build build -j 4
KARTA_BASE_URL=http://127.0.0.1:8123/v1 ./karta/build/karta \
  --provider nvidia --model nvidia/nemotron-3.5-lightning-30b-a3b \
  --mode json -p "Use bash: uname -s"
```

### Windows (cmd.exe)

```cmd
cd karta
cmake -S . -B build
cmake --build build
karta\build\karta.exe --provider nvidia --model nvidia/nemotron-3.5-lightning-30b-a3b ^
  --mode json -p "Use powershell: echo hello"
```

### Key management

- `/login` saves provider keys to `$HOME/.karta/auth.json` (0700 directory, 0600 file, atomic write)
- `/logout` removes the saved key for a provider
- `karta/api.txt` contains `NVIDIA_API_KEY=nvapi-...` (85 bytes, gitignored)
- `.gitignore` excludes `karta/api.txt`, `karta/build/`, and `reference/`
- `chmod 600 karta/api.txt` recommended
- Keys come from the provider env var, the saved auth file, or `--api-key` — never printed and never sent at login time

## Verification evidence

- Build: `cmake -S . -B build && cmake --build build -j 4` exits 0 with `-Wall -Wextra`
- Mock OpenAI SSE: streaming, split-argument tool calls, bash tool, second model turn, final text, JSONL session with correct parent chain
- Multi-tool batch (write/edit/find/ls): all 4 tools applied in one turn
- Session resume (`-c`): 13 entries across two turns
- JSON mode: emits `message_end` with nested message object (pi-compatible)
- Real model: `NVIDIA_API_KEY=$(cat karta/api.txt) karta --provider nvidia --model nvidia/nemotron-3.5-lightning-30b-a3b -p "uname -s"` → drives bash → `Linux`
- RPC mode: `echo '{"type":"prompt","text":"RPC hello"}' | karta --mode rpc --provider openai --model mock --api-key t` → assistant text `Tool said: hello-from-karta`

## Next steps (if extending the port)

1. Add remaining pi packages (codemode, durable, chord, telemetry, evals)
2. Full MCP handshake implementation
3. TUI extras: markdown rendering, themes, multi-line editor
4. Extended tool set (glob improvements, recursive find)
5. Additional provider back-ends (openrouter, xai, deepseek)