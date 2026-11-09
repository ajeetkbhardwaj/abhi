# TUI Verification Report (u7a–u7d)

## Comparison: pi `packages/tui/src/` vs Karta `karta/src/tui/`

---

### u7a: pi TUI Component Mapping

| pi Component | Karta Component | Status | Notes |
|--------------|-----------------|--------|-------|
| `tui.ts` (differential renderer) | `screen.cpp` — full `erase()`/redraw per frame | ⚠️ **Different approach** | pi: differential (only changed lines); Karta: full redraw |
| `terminal.ts` (modes, sizing) | `Tui::init`/`stop`/`KEY_RESIZE` | ✅ | |
| `editor-component.ts` / `components/editor.ts` | Single-line editor in `screen.cpp::poll()` | ⚠️ **Single-line only** | pi: multi-line, kill-ring, undo, word-nav, autocomplete |
| `components/select-list.ts` | `select-list.cpp` + `model-picker.cpp` | ✅ | Karta: bounded to visible rows |
| `components/loader.ts` | `loader.cpp` — spinner/status | ✅ | |
| `components/scroll-view.ts` | `scroll-view.cpp` — transcript scrollback | ✅ | PgUp/PgDn, auto-scroll |
| `components/text.ts` / `truncated-text.ts` | `text.cpp` — word wrap + truncation | ✅ | |
| `keybindings.ts` / `keys.ts` | `screen.h::Key` enum + `handle_key()` | ⚠️ **Subset** | pi: 200+ keys; Karta: ~30 |
| `components/markdown.ts` | **Missing** | ❌ | Transcript is plain text |
| `components/image.ts` / `terminal-image.ts` | **Missing** | ❌ | |
| `kill-ring.ts` / `undo-stack.ts` / `word-navigation.ts` | **Missing** | ❌ | History + backspace/delete only |
| `autocomplete.ts` / `fuzzy.ts` | **Missing** | ❌ | Slash-command only |
| `colors.ts` / `oklab.ts` | **Missing** | ❌ | 6 fixed ncurses color pairs |
| `tui-alt-screen.ts` / mouse regions / clipboard | **Missing** | ❌ | |

---

### Rendering Architecture Difference

| Aspect | pi (`tui.ts`) | Karta (`screen.cpp`) |
|--------|---------------|---------------------|
| **Strategy** | Differential: compute line diffs, emit minimal escape sequences | Full redraw: `erase()` + render all visible lines each frame |
| **Performance** | Optimized for low latency, high throughput | Simpler, adequate for typical terminal sizes |
| **Cursor handling** | Hardware cursor via `CURSOR_MARKER` APC | Hardware cursor via `move()` to input line |
| **Focus** | `Focusable` interface with `focused` flag | Implicit: input line always focused in Chat phase |
| **Overlays** | Generic `Overlay` system with anchors | Model picker only (hardcoded overlay) |

---

### u7b: Keybinding Coverage

| Key | pi Action | Karta Action | Match |
|-----|-----------|--------------|-------|
| `Enter` | Submit / select completion | ✅ Submit / select completion | ✅ |
| `Esc` | Cancel / close overlay / abort | ✅ Cancel turn/fetch/phase, close completion | ✅ |
| `Ctrl+C` | Cancel turn / quit at rest | ✅ Cancel turn, quit at rest | ✅ |
| `Ctrl+D` | Quit at empty idle prompt | ✅ Quit at empty idle prompt | ✅ |
| `Ctrl+L` | Refresh/redraw | ✅ Redraw | ✅ |
| `↑`/`↓` | History / completion nav | ✅ History / completion nav | ✅ |
| `Tab` | Cycle completion | ✅ Cycle completion | ✅ |
| `←`/`→` | Cursor movement | ✅ Cursor movement | ✅ |
| `Home`/`End` | Line start/end | ✅ Line start/end | ✅ |
| `Backspace`/`Delete` | Edit (UTF-8) | ✅ Edit (UTF-8) | ✅ |
| `PageUp`/`PageDown` | Scroll transcript | ✅ Scroll transcript | ✅ |
| `Ctrl+←`/`Ctrl+→` | Word navigation | ❌ Not implemented | ❌ |
| `Ctrl+K` | Kill to end of line | ❌ Not implemented | ❌ |
| `Ctrl+U` | Kill to start of line | ❌ Not implemented | ❌ |
| `Ctrl+W` | Kill word backward | ❌ Not implemented | ❌ |
| `Ctrl+Y` | Yank (kill-ring) | ❌ Not implemented | ❌ |
| `Alt+.` | Insert last argument | ❌ Not implemented | ❌ |
| `Ctrl+R` | History search | ❌ Not implemented | ❌ |
| Mouse wheel | Scroll transcript | ✅ Scroll transcript only | ✅ |
| Mouse click | Focus / select | ❌ Not implemented | ❌ |

**pi keybinding system** (`keybindings.ts`):
- Configurable via `keybindings.json`
- Supports sequences, chords, modes
- 200+ default bindings

**Karta keybinding system** (`screen.cpp::poll()`):
- Hardcoded in `handle_key()`
- ~30 bindings
- No configuration

---

### u7c: Command Flow Verification

| Command | pi Behavior | Karta Behavior | Match |
|---------|-------------|----------------|-------|
| `/login [provider]` | Connect provider; masked key input; save to auth; no validation call | ✅ Same | ✅ |
| `/logout [provider]` | Forget saved key (default: current) | ✅ Same | ✅ |
| `/model [query]` | Load models from **all** logged-in providers; searchable picker | ✅ Same | ✅ |
| `/model <provider/model>` | Direct set | ✅ Same | ✅ |
| `/provider [name]` | Show/switch/start login | ✅ Same | ✅ |
| `/thinking [0-3\|off\|low\|medium\|high]` | Set thinking level | ✅ Same | ✅ |
| `/session` | Print session JSONL path | ✅ Same | ✅ |
| `/clear` | Clear transcript | ✅ Same | ✅ |
| `/help` | List commands | ✅ Same | ✅ |
| `/exit` | Quit (works from every phase) | ✅ Same | ✅ |

**Slash-command autocomplete:**
| Aspect | pi | Karta |
|--------|-----|-------|
| Trigger | Type `/` | Type `/` |
| Filtering | As you type | As you type |
| Navigation | `↑`/`↓`/`Tab` | `↑`/`↓`/`Tab` |
| Selection | `Enter` | `Enter` |
| Close | `Esc` | `Esc` |
| Commands | All slash commands | All slash commands |

---

### u7d: Remaining TUI Features (Decision)

| Feature | pi Status | Karta Status | Effort | Recommendation |
|---------|-----------|--------------|--------|----------------|
| **Markdown rendering** | ✅ `components/markdown.ts` (CommonMark + GFM) | ❌ Plain text | High | Defer — major feature |
| **Image rendering** | ✅ `terminal-image.ts` (Kitty/Sixel/iTerm2) | ❌ | High | Defer — requires terminal support |
| **Multi-line editor** | ✅ `editor.ts` (UTF-8, kill-ring, undo, word-nav) | ❌ Single-line only | High | Defer — complex |
| **Themes/colors** | ✅ `colors.ts` + `oklab.ts` (16-color + truecolor) | ❌ 6 fixed pairs | Medium | Add theme config file |
| **Kill-ring / undo** | ✅ `kill-ring.ts` + `undo-stack.ts` | ❌ | Medium | Add for power users |
| **Word navigation** | ✅ `word-navigation.ts` | ❌ | Low | Add `Ctrl+←/→`, `Ctrl+W` |
| **Full autocomplete** | ✅ `autocomplete.ts` + `fuzzy.ts` | ❌ Slash only | Medium | Add for paths, symbols |
| **Mouse support** | ✅ Mouse regions, click, drag | ❌ Wheel only | Medium | Add click-to-focus |
| **Clipboard** | ✅ OSC 52 + system clipboard | ❌ | Low | Add OSC 52 |
| **Alt screen** | ✅ `tui-alt-screen.ts` (full-screen apps) | ❌ | High | Defer |

---

## Summary: u7a–u7d Verification Results

| UUID | Task | Status | Priority |
|------|------|--------|----------|
| **u7a** | Component mapping | ⚠️ **PARTIAL** — core components ported; rendering architecture differs | Medium |
| **u7b** | Keybinding coverage | ❌ **FAIL** — ~30 vs 200+ bindings; no word nav, kill-ring, history search | Medium |
| **u7c** | Command flows (`/login`, `/model`, `/provider`) | ✅ **PASS** — all core commands work identically | — |
| **u7d** | Remaining features decision | 📋 **DOCUMENTED** — see table above | Low |

---

## Required Fixes for Parity

### Medium Priority (UX Polish)
1. **Add word navigation** — `Ctrl+←/→`, `Ctrl+W` (kill word), `Alt+.` (last arg)
2. **Add basic kill-ring** — `Ctrl+K` (kill line), `Ctrl+U` (kill to start), `Ctrl+Y` (yank)
3. **Add history search** — `Ctrl+R` reverse search
4. **Add theme configuration** — JSON file for 6 color pairs (background, text, accent, dim, error, success)

### Low Priority (Power User)
5. **Multi-line editor** — major rewrite of `screen.cpp::poll()`; use `editor.cpp` as base
6. **Markdown rendering** — integrate `cmark` or similar; transcript becomes rich
7. **Full autocomplete** — paths, symbols, model names (beyond slash commands)
8. **Mouse click handling** — focus components, click links

### Defer (Major Features)
9. **Image rendering** — requires terminal capability detection
10. **Alt screen** — for full-screen apps (pagers, editors)

---

## Effort Estimate

| Fix | Effort | Risk |
|-----|--------|------|
| Word nav + kill-ring + history search (1-3) | 2-3 days | Low |
| Theme config (4) | 1 day | Low |
| Multi-line editor (5) | 1-2 weeks | High |
| Markdown rendering (6) | 1 week | Medium |
| Full autocomplete (7) | 3-4 days | Medium |
| Mouse clicks (8) | 2 days | Low |
| Clipboard (9) | 1 day | Low |

---

## Decision: Target Scope

**Recommendation**: Implement **word navigation + kill-ring + history search + theme config** (items 1-4) — high UX impact, low effort. Defer multi-line editor, markdown, images.

**Immediate action items**:
1. Add `Ctrl+←/→`, `Ctrl+W`, `Alt+.` to `screen.cpp::poll()`
2. Add `Ctrl+K`, `Ctrl+U`, `Ctrl+Y` with simple kill-ring (vector<string>)
3. Add `Ctrl+R` history search mode
4. Add `~/.karta/theme.json` for 6 color pairs; load in `Tui::init()`