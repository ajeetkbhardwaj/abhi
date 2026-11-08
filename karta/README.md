# Karta — Device-Agnostic C++17 Agent

Karta is a C++17 port of the pi agent core (ncurses TUI + agent loop, tools, sessions, provider bridge). Designed to run on Linux, macOS, and Windows with minimal dependencies.

## Build

```bash
cd karta
cmake -S . -B build -DKARTA_BUILD_TUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

- Binary: `karta/build/karta`
- TUI build requires `ncurses`/`ncursesw` (`pkg-config ncursesw` preferred) + `libcurl`.
- Launchers: `./run-tui.sh`, `../run-karta-tui.sh` (build if needed, run TUI).
- Convenience command (workspace): `~/.local/bin/karta -> karta/build/karta` (`karta --version`).

## Run (pi-style)

```bash
karta                  # TTY + no prompt => terminal UI
karta -p "hello"       # one-shot/print-style prompt
karta --mode text
karta --mode json -p "Use bash to list /tmp"
karta --mode rpc       # JSONL control
```

## Terminal UI

When attached to a TTY with no prompt, Karta opens the ncurses TUI.

**Commands**
- `/login [provider]` — connect a provider; API key input is masked and saved immediately (never test-called at login). Saved to `$HOME/.karta/auth.json` (dir `0700`, file `0600`, atomic write). `/logout [provider]` removes it.
- `/model` — loads models from **all logged-in/available providers** (saved auth + env/local) and opens searchable selection. Type `/model <query>` to filter; navigate with `↑/↓` or `Tab`, `Enter` to select/apply (`provider/model`). Direct set: `/model <provider/model>` or `/model <id>`.
- `/provider [name]`, `/thinking [0|1|2|3|off|low|medium|high]` (default 1/low), `/session`, `/clear`, `/help`, `/exit`.

**Keys / behavior**
- `Enter` submit, `Esc` cancel running turn/fetch/phase, `Ctrl+C` cancel or quit at rest, `Ctrl+D` quit at empty idle prompt, `Ctrl+L` refresh.
- Slash-command autocomplete: typing `/` shows commands; `↑/↓/Tab` navigate, `Enter` select, `Esc` close (filters as you type).
- Mouse wheel scrolls the **transcript** area (not the input). `PgUp/PgDn` scroll transcript.
- Input is framed by horizontal rules above and below; status/spinner (thinking/generating) shows just above input.
- Assistant output streams live; reasoning/thinking deltas are shown when provided (`[thinking]…[/thinking]`) and controlled by thinking level. Tool output is rendered in the transcript (scrollable).
- UTF-8/wide-char rendering via ncursesw + locale.

Details and pi-component mapping: `docs/TUI.md`. Pipes and `-p` still use `--mode text|json|rpc`.

## Supported providers

| Provider  | Endpoint                    | Env var           | Models               |
|-----------|-----------------------------|-------------------|----------------------|
| openai    | https://api.openai.com/v1   | OPENAI_API_KEY    | gpt-4o-mini, etc.    |
| anthropic| https://api.anthropic.com   | ANTHROPIC_API_KEY | claude-3-5-sonnet-4-5|
| nvidia    | https://integrate.api.nvidia.com/v1 | NVIDIA_API_KEY | nemotron-3.5-lightning-30b-a3b, etc. |

## Tool set (ported)

| Tool | Status |
|------|--------|
| bash   | ✅    |
| read   | ✅    |
| write  | ✅    |
| edit   | ✅    |
| grep   | ✅    |
| find   | ✅    |
| ls     | ✅    |

## Session format (JSONL v3)

- Header: `type`, `version`, `id`, `timestamp`, `cwd`
- Entries: `message`, `model_change`, `session_info`, `compaction`
- `find_latest` and `find_by_id` for session resume (`-c`)

## Options

```
--provider <name>          Provider: anthropic, openai, openrouter, groq,
                           deepseek, xai, mistral, nvidia, ollama, lmstudio
--model <id>               Model id (default varies by provider)
--mode <text|json|rpc>     Output mode
--tui                      Force the terminal UI (default on a TTY)
-p <prompt>                Prompt
-c, --session-id <id>      Use/create session by id
--fork <path>              Fork into this directory
--compact-threshold <n>    Bytes before temp-file spill
--max-turns <n>            Max turns (default 20)
```

## Portability

- No external JSON library — hand-written parser
- libcurl for HTTP transport
- `std::filesystem` + `platform.cpp` abstraction (POSIX fork/pipe vs Win32 CreateProcess)
- Tools run via `/bin/sh -c` (POSIX) or `cmd.exe /d /s /c` (Windows)
- No `rg`/`fd` reliance — internal lightweight implementations
- `.gitignore` includes `karta/api.txt` and `reference/` and `karta/build/`

## Feature parity matrix (per original pi package)

| Package         | Ported | Notes                          |
|-----------------|--------|--------------------------------|
| packages/agent  | ✅     | Core loop, tool batches, events|
| packages/ai     | ❌     | LLM integration (uses provider)|
| packages/coding-agent | 🟡 | Tools + CLI + sessions ported; editors/MCP not |
| packages/tui    | ✅     | ncurses TUI, `/login`, `/model` (see docs/TUI.md) |
| packages/mcp      | ❌     | MCP handshake not ported       |
| packages/codemode | ❌    | Not ported                     |
| packages/durable| ❌     | Not ported                     |
| packages/chord  | ❌     | Not ported                     |
| packages/telemetry | ❌   | Not ported                     |
| packages/evals  | ❌     | Not ported                     |

For full details, see `karta/docs/FEATURE_PARITY.md`.