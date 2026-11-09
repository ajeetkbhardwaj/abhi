# Agent Core — Done vs Remaining

## Source Reference
- pi: `reference/pi/packages/agent/src/`
- Karta: `karta/src/agent.cpp`, `karta/src/prompt.cpp`, `karta/include/karta/agent/agent.h`

---

## ✅ Done (Ported to Karta)

| Feature | pi Location | Karta Location | Notes |
|---------|-------------|----------------|-------|
| Agent main loop | `agent-loop.ts` | `agent.cpp::run_loop()` | Steering/follow-up queues, turn counting |
| Steering queue | `agent-loop.ts` | `agent.cpp::queue_steering()` / `drain_steering()` | In-turn injections |
| Follow-up queue | `agent-loop.ts` | `agent.cpp::queue_followup()` / `drain_followups()` | Post-turn continuations |
| Tool batch execution | `agent-loop.ts` | `agent.cpp::execute_tool_calls()` | Ordered, unique, non-overlapping oldText |
| Tool batch truncation guard | `agent-loop.ts` | `agent.cpp::fail_truncated_tool_calls()` | 2000 lines / 50KB cap |
| Event system | `events.ts` | `agent.cpp::emit()` + `AgentEvent` | message_start/end, tool_execution_start/end, turn_start/end, agent_start/end |
| Compaction | `agent-loop.ts` | `agent.cpp::maybe_compact()` | Token threshold trigger |
| Session resume (`-c`) | `session.ts` | `session.cpp::find_by_id()` | Parent chain preserved |
| Fork session (`--fork`) | `session.ts` | `cli.cpp` + `session.cpp` | New cwd, copied session |
| Agents.md discovery | `agent-loop.ts` | `agent.cpp` (via CLI) | Upward search, max 3 files, 16KB each |
| System prompt handling | `agent-loop.ts` | `agent.cpp::set_system_prompt()` | Append via `--append-system-prompt` |
| Max turns limit | `agent-loop.ts` | `AgentOptions::max_turns` | Default 20 via CLI |
| Interrupt handling | `agent-loop.ts` | `Agent::interrupt()` / `interrupted()` | Atomic flag, checked in loop |

---

## 🔍 Verification Needed (from plan u4)

| UUID | Task | Status |
|------|------|--------|
| u4a | Verify pi agent events map 1:1 to Karta AgentEvent | ✅ done |
| u4b | Verify tool call/result wire format matches pi | ✅ done |
| u4c | Verify tool execution modes (parallel/sequential) map | ✅ done |
| u4d | Verify beforeToolCall/afterToolCall hook coverage | ✅ done |
| u4e | Verify finishTurn and prepareNextTurn coverage | ✅ done |
| u4f | Verify session resume and fork behavior | ✅ done |

---

## 🔧 Gaps to Fix (from verification)

| UUID | Gap | Priority | Effort | Description |
|------|-----|----------|--------|-------------|
| u4g | Parallel tool execution | High | Medium | Currently sequential; pi runs non-overlapping tool calls in parallel |
| u4h | beforeToolCall / afterToolCall hooks | High | Medium | No hook system for tool interception/validation |
| u4i | Wire format alignment | Medium | Low | AgentEvent fields differ from pi (e.g., `turn_start` vs `turnStart`) |

---

## ❌ Not Ported / Out of Scope

| Feature | pi Location | Reason |
|---------|-------------|--------|
| Agent skills/extensions | `agent-loop.ts` hooks | Requires MCP/extension system (u15) |
| Custom tool registration | `tools.ts` registry | Built-in tools only currently |
| Multi-agent delegation | N/A | Not in pi agent package |

---

## Next Steps (Fix Gaps)
1. **u4g**: Add parallel execution in `execute_tool_calls()` — use thread pool for non-overlapping calls
2. **u4h**: Add `std::function` hook vectors to `AgentOptions`, call before/after each tool
3. **u4i**: Rename `AgentEvent` fields to match pi camelCase convention