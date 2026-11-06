# Karta Terminal UI — reverse-engineering notes

The TUI is a ncurses rewrite of `pi`'s `packages/tui`, wired to the ported agent
core. This document records what each pi piece became, so later work can extend
the port instead of rediscovering it.

## Layout

```
row 0            header: karta <version> · provider · model · cwd
row 1            rule
rows 2..n-4      transcript (wrapped, scrollable)
row n-3          status (spinner + message)
row n-2          rule
row n-1          input line: prompt + editable text + cursor
```

## pi → karta component map

| pi (`packages/tui/src/`) | karta (`karta/src/tui/`) | Status |
|--------------------------|--------------------------|--------|
| `tui.ts` differential renderer | `screen.cpp` — one full `erase()`/redraw per changed frame on ncurses | ✅ |
| `terminal.ts` (modes, sizing) | `Tui::init` / `stop` / `KEY_RESIZE` | ✅ |
| `editor-component.ts`, `components/editor.ts`, `components/input.ts` | single-line editor in `screen.cpp` (`poll`, UTF-8-aware cursor, history) | ✅ (single line) |
| `components/select-list.ts` | numbered model picker in `app.cpp` (`/model`, `/login`), bounded to the visible rows | ✅ |
| `components/loader.ts` | status-line spinner with busy flag | ✅ |
| `components/scroll-view.ts` | transcript scrollback, `PageUp`/`PageDown`, auto-scroll on new output | ✅ |
| `components/text.ts`, `components/truncated-text.ts` | `append_text` word wrap + tool-result first-line truncation | ✅ |
| `keybindings.ts`, `keys.ts` | `enum class Key` in `screen.h` | ✅ (subset) |
| `components/markdown.ts` | transcript is plain text | ❌ |
| `components/image.ts`, `terminal-image.ts` | not ported | ❌ |
| `kill-ring.ts`, `undo-stack.ts`, `word-navigation.ts` | not ported (history + backspace/delete only) | ❌ |
| `autocomplete.ts`, `fuzzy.ts` | not ported | ❌ |
| `colors.ts`, `oklab.ts` | 6 fixed ncurses color pairs | ❌ (no themes) |
| `tui-alt-screen.ts`, mouse regions, clipboard | not ported | ❌ |

## Threading model

ncurses is single-threaded, so nothing else touches the screen:

- The **agent** runs on a worker thread; events are pushed into a mutex-guarded
  queue and drained by the UI loop (`consume_events`).
- **Model fetching** runs on a second worker (`start_job`) with an atomic cancel
  flag passed into `fetch_models` — quitting or pressing `Esc` aborts the HTTP
  transfer instead of blocking the join.
- All drawing, key handling, and session appends happen on the UI thread.

## Keys

| Key | Action |
|-----|--------|
| `Enter` | submit the line |
| `Esc` | cancel the running turn / in-flight fetch / current prompt phase |
| `Ctrl+C` | cancel a turn; at rest, quit |
| `↑` / `↓` | command history |
| `←` / `→`, `Home` / `End` | cursor movement |
| `Backspace` / `Delete` | edit (UTF-8 aware) |
| `PageUp` / `PageDown` | scroll transcript |

## Commands

| Command | Behavior |
|---------|----------|
| `/login [provider]` | prompt for provider, then a masked API key; the key is **saved immediately** (never tested against the provider) and the model list is fetched |
| `/logout [provider]` | forget the saved key (defaults to the current provider) |
| `/model [id]` | fetch the provider catalog and show a numbered picker; `/model <id>` sets a model directly |
| `/provider [name]` | show the provider, or start `/login` for another one |
| `/session` | print the session JSONL path |
| `/clear` | clear the transcript |
| `/help` | list commands |
| `/exit` | quit (works from every prompt phase) |

Commands are recognized before phase dispatch, so `/exit` can never be stored as
a provider name, key, or model id.

## Credentials

- Location: `$HOME/.karta/auth.json`, one provider → key entry.
- POSIX: directory `0700`, file `0600` from creation (the file is opened with
  mode `0600`, not chmod-ed afterwards), written to `auth.json.tmp` and renamed
  atomically so a crash never truncates it.
- Windows: written through `std::ofstream`; ACLs rely on the user profile.
- Resolution order: provider env var → stored file → `KARTA_API_KEY` →
  `OPENAI_API_KEY` → `ANTHROPIC_API_KEY`.
- Keys are never printed, logged, or placed in argv; the input line masks them.
- No validation request is made on login, so a pasted key is not spent before
  the user's first prompt.
