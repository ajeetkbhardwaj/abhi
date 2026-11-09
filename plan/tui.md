# TUI — Done vs Remaining

## Source Reference
- pi: `reference/pi/packages/tui/src/`, `reference/pi/packages/tui/native/`
- Karta: `karta/src/tui/` — `screen.cpp`, `app.cpp`, `model-picker.cpp`, `select-list.cpp`, `text.cpp`, `loader.cpp`, `scroll-view.cpp`, `editor.cpp`
- Docs: `karta/docs/TUI.md`

---

## ✅ Done (Ported to Karta)

| pi Component | Karta Component | Status | Notes |
|--------------|-----------------|--------|-------|
| `tui.ts` differential renderer | `screen.cpp` — full `erase()`/redraw per frame | ✅ | ncurses immediate mode |
| `terminal.ts` (modes, sizing) | `Tui::init` / `stop` / `KEY_RESIZE` | ✅ | |
| `editor-component.ts`, `components/editor.ts`, `components/input.ts` | Single-line editor in `screen.cpp` (`poll`) | ✅ | UTF-8 cursor, history, masked input |
| `components/select-list.ts` | Numbered model picker in `app.cpp` (`/model`, `/login`) | ✅ | Bounded to visible rows |
| `components/loader.ts` | Status-line spinner with busy flag | ✅ | |
| `components/scroll-view.ts` | Transcript scrollback, PgUp/PgDn, auto-scroll | ✅ | |
| `components/text.ts`, `components/truncated-text.ts` | `append_text` word wrap + tool-result first-line truncation | ✅ | |
| `keybindings.ts`, `keys.ts` | `enum class Key` in `screen.h` | ✅ | Subset |
| Slash-command autocomplete | `app.cpp` — typing `/` opens filtered list | ✅ | ↑/↓/Tab navigate, Enter select, Esc close |
| Agent worker thread | `app.cpp` — events pushed to mutex queue | ✅ | Drained by UI loop `consume_events` |
| Model-fetch worker | `app.cpp` — `start_job` with atomic cancel flag | ✅ | Esc aborts HTTP transfer |
| `/login [provider]` | `app.cpp::handle_login()` | ✅ | Masked input, saved to `~/.karta/auth.json` |
| `/logout [provider]` | `app.cpp::handle_logout()` | ✅ | Removes saved key |
| `/model [query]` | `app.cpp::handle_model()` | ✅ | Loads from all logged-in providers, searchable |
| `/provider [name]` | `app.cpp::handle_provider()` | ✅ | Show/switch/start login |
| `/thinking [0-3\|off\|low\|medium\|high]` | `app.cpp::handle_thinking()` | ✅ | Default 1/low |
| `/session`, `/clear`, `/help`, `/exit` | `app.cpp` handlers | ✅ | |

### Layout (Implemented)
```
row 0              header: karta <version> · provider · model · cwd
row 1              rule
rows 2..n-5        transcript (wrapped, scrollable; mouse wheel over transcript only)
row n-4            status/spinner (ready, thinking/generating, loading) + completion overlay
row n-3            rule
row n-2            input: prompt + editable text + cursor
row n-1            rule
```

### Keys (Implemented)
| Key | Action |
|-----|--------|
| `Enter` | Submit line / select completion |
| `Esc` | Cancel turn / fetch / phase; close completion |
| `Ctrl+C` | Cancel turn; at rest, quit |
| `Ctrl+D` | Quit at empty idle prompt |
| `Ctrl+L` | Refresh/redraw |
| `↑`/`↓` | History / completion navigation |
| `Tab` | Cycle completion |
| `←`/`→`, `Home`/`End` | Cursor movement |
| `Backspace`/`Delete` | Edit (UTF-8 aware) |
| `PageUp`/`PageDown` | Scroll transcript |
| Mouse wheel | Scroll transcript only |

### Credentials (Implemented)
- Location: `$HOME/.karta/auth.json`
- POSIX: dir `0700`, file `0600` from creation, atomic write via `.tmp` rename
- Resolution order: env var → stored file → `KARTA_API_KEY` → `OPENAI_API_KEY` → `ANTHROPIC_API_KEY`
- Keys never printed, logged, or in argv; masked on input
- No validation request on login

---

## 🔍 Verification Needed (from plan u7)

| UUID | Task | Status |
|------|------|--------|
| u7a | Map pi TUI components to Karta (detailed) | ✅ done |
| u7b | Verify keybinding coverage | ✅ done |
| u7c | Verify `/login` `/logout` `/model` flows | ✅ done |
| u7d | Add remaining TUI features or document out of scope | ✅ done |

---

## 🔧 Gaps to Fix (from verification)

| UUID | Gap | Priority | Effort | Description |
|------|-----|----------|--------|-------------|
| u7e | Markdown rendering in transcript | High | High | Major feature — pi has `components/markdown.ts` |
| u7f | Extended keybinding coverage | Medium | Medium | Target ~100+ (currently ~30) |
| u7g | Multi-line editor support | Medium | High | Single-line only currently |
| u7h | Syntax highlighting | Low | Medium | Not in pi either |
| u7i | Themes/color configuration | Low | Low | 6 fixed ncurses color pairs |

---

## ❌ Not Ported / Out of Scope

| pi Component | Karta Status | Notes |
|--------------|--------------|-------|
| `components/markdown.ts` | ❌ | Transcript is plain text |
| `components/image.ts`, `terminal-image.ts` | ❌ | Not ported |
| `kill-ring.ts`, `undo-stack.ts`, `word-navigation.ts` | ❌ | History + backspace/delete only |
| `autocomplete.ts`, `fuzzy.ts` | ❌ | Slash-command only |
| `colors.ts`, `oklab.ts` | ❌ | 6 fixed ncurses color pairs, no themes |
| `tui-alt-screen.ts`, mouse regions, clipboard | ❌ | Not ported |
| Multi-line editor | ❌ | Single-line only |
| Syntax highlighting | ❌ | Not in pi either |

---

## Next Steps (Fix Gaps)
1. **u7e**: Add markdown rendering — integrate `md4c` or similar C library
2. **u7f**: Add missing keybindings — copy from pi `keybindings.ts`
3. **u7g**: Multi-line editor — significant port of `editor.ts`/`input.ts`

## TUI Architecture Notes

### Threading Model
- ncurses is single-threaded — only UI thread touches screen
- **Agent** runs on worker thread; events → mutex queue → UI loop `consume_events`
- **Model fetching** runs on second worker (`start_job`) with atomic cancel flag
- All drawing, key handling, session appends on UI thread

### Files
| File | Purpose |
|------|---------|
| `screen.h/cpp` | ncurses init/stop, drawing primitives, input polling, key enum |
| `app.h/cpp` | Main TUI app, command handlers, agent/model workers, layout |
| `model-picker.h/cpp` | `/model` searchable selection UI |
| `select-list.h/cpp` | Generic numbered selection list (bounded) |
| `text.h/cpp` | Word-wrap, truncation helpers |
| `loader.h/cpp` | Spinner/status-line |
| `scroll-view.h/cpp` | Transcript scrollback |
| `editor.h/cpp` | Single-line editor (UTF-8, history, masking) |

---

## Next Steps
1. Run verification tasks u7a–u7d against `reference/pi/packages/tui/src/`
2. Document any keybinding or flow gaps
3. Decide on markdown rendering (major feature) vs keep plain text
4. Consider adding themes/color config (low priority)
5. Multi-line editor would require significant editor.ts port