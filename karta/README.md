# Karta — Device-Agnostic C++17 Agent

Karta is a faithful C++17 port of the pi agent core ("each and every features from typescript to the c/cpp"). Works on any device: Linux, macOS, Windows — no platform-specific dependencies.

## Build

```bash
cd karta
cmake -S . -B build -DKARTA_BUILD_TUI=ON   # needs ncurses
cmake --build build -j 4
```

Binary: `karta/build/karta`

## Quick start

```bash
# JSON mode (events on stdout)
KARTA_BASE_URL=http://127.0.0.1:8123/v1 ./karta/build/karta \
  --provider nvidia --model nvidia/nemotron-3.5-lightning-30b-a3b \
  --mode json -p "Use bash to list /tmp" --no-session

# Text mode (REPL)
./karta/build/karta --mode text -p "What's my OS?"
```

## Terminal UI

```bash
./karta/build/karta
```

With no prompt (and a TTY) karta opens the ncurses UI. From there:

```
/login            # pick a provider, paste its API key (masked, saved)
/model            # list that provider's models and pick one
/exit
```

The key is stored in `$HOME/.karta/auth.json` (0600, directory 0700, atomic
write) so the next launch is already connected. `/logout` removes it. Keys are
never printed or sent anywhere at login time. See `karta/docs/TUI.md`.

Pipes and `-p` still use the plain text/JSON/RPC modes.

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