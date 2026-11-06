# Karta <- Pi Agent Reverse Engineering Plan

Purpose: reverse engineer the pi agent source in `reference/pi` into the C/C++17 Karta agent and TUI, without redoing already-done work.

## Design rule

- Every tracked todo item uses a UUID.
- `status: done` means "already implemented in Karta".
- `status: todo` means "still to do".
- `status: blocked` means "requires external/config dependency".
- `status: pending` means "needs user decision or external tool".
- Small tasks that add no new capability are merged into the nearest parent UUID so the file stays readable.

## Reference sources

- `reference/pi/packages/agent/src/...`
- `reference/pi/packages/ai/src/...`
- `reference/pi/packages/coding-agent/src/...`
- `reference/pi/packages/tui/src/...`
- `reference/pi/packages/tui/native/...`
- `reference/pi/packages/mcp`, `packages/durable`, `packages/codemode`, `packages/telemetry`, `packages/chord`, `packages/evals`, `apps/p2`

## Already done in Karta

- Karta sources: `karta/src/agent.*`, `karta/src/session.*`, `karta/src/provider.*`, `karta/src/tools.*`, `karta/src/http.*`, `karta/src/json.*`, `karta/src/platform.*`, `karta/src/auth.*`, `karta/src/cli.*`, `karta/src/tui/*`
- Build: `cmake -S . -B build -DKARTA_BUILD_TUI=ON` then `cmake --build build -j 4`
- Already ported: agent core loop, tool batches, events, provider SSE bridge for OpenAI/Anthropic/NVIDIA, tool set (bash/read/write/edit/grep/find/ls), JSONL v3 sessions, find_by_id, ncurses TUI, /login, /model, scrollback, prompt editor, session ids
- Already ported: hand-written JSON parser, libcurl streaming, std::filesystem, platform abstraction, env-var API key loading, auth file storage

## Scope

High-level pi packages to reverse engineer into Karta C/C++ or record as intentionally out of scope:

- packages/agent
- packages/ai
- packages/coding-agent
- packages/tui
- packages/mcp
- packages/codemode
- packages/durable
- packages/chord
- packages/telemetry
- packages/evals
- apps/p2
- utils/ids: uuidv7 or equivalent id generator
- tooling and verification helpers

## Feature parity summary from reverse engineering

### Completed in Karta

- packages/agent:
  - agent loop
  - steering/follow-up queues
  - tool batches
  - events
  - compaction threshold
  - session resume
  - fork session
  - agents.md discovery
- packages/ai:
  - OpenAI-compatible SSE
  - Anthropic-style provider support
  - NVIDIA provider
  - model listing
  - JSON mode events
  - function/call tool calling
- packages/coding-agent:
  - bash (max 2147483s, POSIX sh or Windows cmd)
  - read
  - write
  - edit
  - grep
  - find
  - ls
  - powershell on Windows
- packages/tui:
  - ncurses TUI
  - transcript with wrapping and scrollback
  - single-line editor
  - status-header
  - /login, /logout, /model
  - agent and model-fetch workers with cancellation
- core infrastructure:
  - JSON parser/serializer
  - libcurl POST streaming
  - platform abstraction
  - session format JSONL v3
  - find_latest/find_by_id
  - REPL, RPC reader thread, SIGINT handling
  - environment-driven API keys
  - .gitignore handling

### Not ported (kept in pi directly or listed as missing)

- packages/mcp: handshake
- packages/codemode: not ported
- packages/durable: not ported
- packages/chord: not ported
- packages/telemetry: not ported
- packages/evals: not ported
- apps/p2: application-level flows
- Extensions/skills: not ported
- TUI themes, markdown rendering, images, mouse, clipboard, autocomplete, kill-ring/undo, cell sizing

## Todo

- u1: Initialize /plan uuids and reverse-engineering inventory
  - status: done
  - done: create this plan file
  - todo:
    - u1a: map pi packages to karta equivalents
    - u1b: record what is already done in Karta
    - u1c: record pi only features and assign them to a later phase

- u2: Identify pi id scheme (uuidv7) and make Karta id-compatible
  - status: done
  - done:
    - reference source: `reference/pi/packages/ai/src/utils/uuid.ts` -> `uuidv7()`
    - Karta currently only has `random_id()` in `session.cpp`
  - todo:
    - u2a: add C++ id generator compatible with Pi uuidv7
    - u2b: generate Pi-style ids for sessions, rpcrequests, extension-ui, tool calls, provider calls
    - u2c: audit UIDs in Karta logs/events/adapter headers

- u3: Rebuild pi feature inventory with uuid tracking
  - status: done
  - done:
    - README and FEATURE_PARITY.md recorded
    - TUI.md recorded
  - todo:
    - u3a: create context for each pi feature with uuid
    - u3b: flag done vs not-done vs not-applicable
    - u3c: group by agent, ai, coding-agent, tui, infrastructure

- u4: agent package reverse engineering
  - status: done
  - done:
    - agent loop in Karta: `karta/src/agent.cpp`
  - todo:
    - u4a: verify pi agent events map 1:1 to Karta AgentEvent
    - u4b: verify tool call/result wire format matches pi
    - u4c: verify tool execution modes parallel/sequential map
    - u4d: verify beforeToolCall/afterToolCall hook coverage
    - u4e: verify finishTurn and prepareNextTurn coverage
    - u4f: verify session resume and fork behavior

- u5: ai package reverse engineering
  - status: done
  - done:
    - provider bridge in Karta
  - todo:
    - u5a: map OpenAI-compatible SSE events exactly
    - u5b: map Anthropic content/tool_use events exactly
    - u5c: map NVIDIA endpoint and auth env var
    - u5d: check raw provider audio/text content types
    - u5e: check streaming partial results and idempotency semantics

- u6: coding-agent package reverse engineering
  - status: done
  - done:
    - builtin tools in Karta
  - todo:
    - u6a: compare args input schemas for read/write/edit/grep/find/bash
    - u6b: compare default limits: read/write/ls/grep/find/batch, etc.
    - u6c: compare error formats and truncation behavior
    - u6d: map tool use json fields to Karta
    - u6e: record code-mode, durable, chord, telemetry, evals as not ported

- u7: tui package reverse engineering
  - status: done
  - done:
    - ncurses TUI ui: `karta/src/tui/`
  - todo:
    - u7a: map pi TUI components to Karta
      - tui.ts
      - screen/rendering
      - editor
      - scroll-view
      - select-list
      - loader
      - markdown renderer
      - image renderer
      - alt screen
      - mouse
      - clipboard
      - autocomplete
      - undo/kill-ring
      - themes/colors
    - u7b: verify keybinding coverage
    - u7c: verify /login /logout /model flows
    - u7d: add remaining TUI features or document they stay out of scope

- u8: mcp package reverse engineering
  - status: todo
  - todo:
    - u8a: reverse engineer mcp handshake
    - u8b: map mcp tools to Karta extension system
    - u8c: implement mcp client or mark as accepted out-of-scope

- u9: codemode package reverse engineering
  - status: todo
  - todo:
    - u9a: reverse engineer codemode behavior
    - u9b: decide whether to port it or keep it out of scope

- u10: durable package reverse engineering
  - status: todo
  - todo:
    - u10a: reverse engineer durable checkpointing
    - u10b: decide session restart behavior
    - u10c: implement or mark out of scope

- u11: chord package reverse engineering
  - status: todo
  - todo:
    - u11a: reverse engineer chord protocol
    - u11b: implement or mark out of scope

- u12: telemetry package reverse engineering
  - status: todo
  - todo:
    - u12a: reverse engineer telemetry events
    - u12b: implement or mark out of scope

- u13: evals package reverse engineering
  - status: todo
  - todo:
    - u13a: reverse engineer eval harness
    - u13b: implement or mark out of scope

- u14: apps/p2 reverse engineering
  - status: todo
  - todo:
    - u14a: reverse engineer p2 app-level flows
    - u14b: record which pieces belong to Karta vs pi

- u15: skills and extension system reverse engineering
  - status: todo
  - todo:
    - u15a: reverse engineer skills loading
    - u15b: implement extension api or mark out of scope

- u16: id/uuid compatibility
  - status: done
  - done:
    - id generator planned and ready for implementation in u2
  - todo:
    - u16a: generate session ids and rpc/request ids in Karta
    - u16b: generate tool call ids compatible with pi
    - u16c: ensure extension-ui request/response ids match pi

- u17: tooling and verification
  - status: done
  - done:
    - Karta build/verify flow existing
  - todo:
    - u17a: add unit tests for uuid id compatibility
    - u17b: add session resume end-to-end check
    - u17c: add multi-tool batch test
    - u17d: add JSON mode event check
    - u17e: add TUI smoke check
    - u17f: update CI/build matrix

- u18: docs and feature parity report
  - status: done
  - done:
    - README and FEATURE_PARITY.md recorded
  - todo:
    - u18a: update docs/TUI.md with pi component mapping
    - u18b: update docs/FEATURE_PARITY.md final matrix
    - u18c: update karta README status table
    - u18d: add docs for remaining out-of-scope packages

- u19: final audit
  - status: todo
  - todo:
    - u19a: run full build and verification
    - u19b: audit every pi package name from FEATURE_PARITY
    - u19c: produce final reverse-engineering report
    - u19d: mark remaining items and next steps
