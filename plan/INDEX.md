# Karta Reverse Engineering — Component Status Index

## Overview
This index links to detailed done/remaining analysis for each major component ported from the pi agent to Karta (C++17).

---

## Component Status Summary

| Component | Ported | Verification | Remaining | Detail File |
|-----------|--------|--------------|-----------|-------------|
| **Agent Core** | ✅ ~95% | ✅ u4a–u4f done | Parallel exec, hooks, wire format | [`agent-core.md`](agent-core.md) |
| **Provider Bridge** | ✅ ~95% | ✅ u5a–u5e done | SSE retry/idempotency, vision | [`provider-bridge.md`](provider-bridge.md) |
| **Tools** | ✅ ~90% | ✅ u6a–u6d done | Error format, structuredContent | [`tools.md`](tools.md) |
| **TUI** | ✅ ~80% | ✅ u7a–u7d done | Markdown rendering, keybindings | [`tui.md`](tui.md) |
| **Session** | ✅ ~95% | ✅ Verified | Atomic writes, secure perms, header format | [`session.md`](session.md) |
| **MCP** | ❌ 0% | ✅ **Option C** | Types ready, full impl deferred | [`mcp.md`](mcp.md) |

---

## Not-Ported pi Packages (Explicitly Out of Scope)

| Package | Status | Plan UUID |
|---------|--------|-----------|
| `packages/codemode` | ❌ Not ported | u9 |
| `packages/durable` | ❌ Not ported | u10 |
| `packages/chord` | ❌ Not ported | u11 |
| `packages/telemetry` | ✅ In-memory adapter ported | u12 |
| `packages/evals` | ❌ Not ported | u13 |
| `apps/p2` | ❌ Not ported | u14 |
| Extensions/Skills | ❌ Not ported | u15 |

---

## Verification Tasks (from plan/README.md)

### High Priority (Block Next Phase)
- [x] **u4a–u4f**: Agent core wire-format verification
- [x] **u5a–u5e**: Provider SSE event mapping verification
- [x] **u6a–u6d**: Tool schema/behavior verification
- [x] **u7a–u7d**: TUI component/keybinding verification

### Verification Gaps to Fix (New)
- [x] **u4g**: Parallel tool execution in agent loop
- [x] **u4h**: beforeToolCall / afterToolCall hook system
- [x] **u4i**: Wire format alignment with pi AgentEvent
- [x] **u5f**: SSE retry logic with exponential backoff
- [x] **u5g**: Idempotency keys for tool calls
- [x] **u5h**: Vision support (image input) for compatible providers
- [x] **u6f**: Error format standardization (match pi exactly)
- [x] **u6g**: structuredContent field for tool results
- [x] **u7e**: Markdown rendering in transcript
- [x] **u7f**: Extended keybinding coverage (target ~100+)
- [x] **u7g**: Multi-line editor support
- [x] **u10a**: Atomic session writes (temp file + rename)
- [x] **u10b**: Secure file permissions (0600) on session files
- [x] **u10c**: JSONL header format alignment with pi v4

### Medium Priority
- [ ] **u9a–u9b**: Codemode scope decision
- [ ] **u10d–u10f**: Durable checkpointing scope decision
- [ ] **u11a–u11b**: Chord protocol scope decision
- [ ] **u13a–u13b**: Evals harness scope decision
- [ ] **u14a–u14b**: Apps/p2 scope decision
- [ ] **u15a–u15b**: Skills/extensions scope decision

### Testing & CI (u17)
- [x] **u17a**: UUID compatibility unit tests
- [ ] **u17b**: Session resume end-to-end test
- [ ] **u17c**: Multi-tool batch test
- [ ] **u17d**: JSON mode event test
- [ ] **u17e**: TUI smoke test
- [x] **u17f**: CI/build matrix update

### Documentation (u18)
- [ ] **u18a**: Update `docs/TUI.md` with pi component mapping
- [ ] **u18b**: Update `docs/FEATURE_PARITY.md` final matrix
- [ ] **u18c**: Update `karta/README.md` status table
- [ ] **u18d**: Add docs for out-of-scope packages

### Final Audit (u19)
- [ ] **u19a**: Full build and verification run
- [ ] **u19b**: Audit every pi package from FEATURE_PARITY
- [ ] **u19c**: Produce final reverse-engineering report
- [ ] **u19d**: Mark remaining items and next steps

---

## Next Phase Targets

### Immediate (This Sprint)
1. ✅ Complete verification tasks u4–u7 against `reference/pi/`
2. ✅ **MCP scope: Option C (out of scope)**
3. ✅ Add missing OpenAI-compatible providers (low effort, high value)

### Short Term
4. ✅ Implement `glob` tool (distinct from `find`)
5. ✅ Add unit tests for all verified components
6. ✅ Update CI to run verification suite

### Medium Term (If Scope Expands)
7. ✅ Markdown rendering in TUI
8. ✅ Durable checkpointing (u10) - atomic writes, secure perms, header format
9. MCP client (u8) — Option C chosen (out of scope)
10. Additional features: vision, embeddings, task/todo/patch/webfetch tools

### Remaining Out-of-Scope Packages (Priority Matrix)
| Package | Agent Impact | Port Effort | Priority |
|---------|--------------|-------------|----------|
| **Codemode** (LSP) | 🔴 Critical | High | **1** |
| **Durable** (checkpointing) | 🟠 High | Medium-High | **2** |
| **Evals** (testing) | 🟠 High | Medium | **3** |
| **Telemetry** (exporters) | 🟡 Medium | Low-Medium | **4** |
| **Chord** (P2P) | 🟢 Low-Med | Very High | **5** (later) |

---

## Quick Reference: pi → Karta Source Mapping

| pi Package | Karta Source |
|------------|--------------|
| `packages/agent/src/` | `karta/src/agent.cpp`, `karta/src/prompt.cpp` |
| `packages/ai/src/` | `karta/src/ai/provider.cpp`, `karta/src/ai/http.cpp` |
| `packages/coding-agent/src/` | `karta/src/tools/tools.cpp` |
| `packages/tui/src/` | `karta/src/tui/*.cpp` |
| `packages/mcp/src/` | `karta/src/mcp/mcp.cpp` (stub) |
| `packages/durable/src/` | — (not ported) |
| `packages/telemetry/src/` | `karta/src/core/telemetry.cpp` |

---

## Commands for Verification Work

```bash
# Build Karta (with TUI)
cd karta && cmake -S . -B build -DKARTA_BUILD_TUI=ON && cmake --build build -j4

# Run Karta
./build/karta --version
./build/karta -p "test prompt"
./build/karta --mode json -p "test"

# View pi source for comparison
ls reference/pi/packages/agent/src/
ls reference/pi/packages/ai/src/
ls reference/pi/packages/coding-agent/src/
ls reference/pi/packages/tui/src/
ls reference/pi/packages/mcp/src/
```

---

## Decision Required

**MCP (u8)**: Choose implementation strategy:
- **A**: Full MCP client in Karta (high effort)
- **B**: External MCP proxy bridge (medium effort)
- **C**: Mark out of scope (current — no effort)

Please advise on MCP direction before u8 work begins.