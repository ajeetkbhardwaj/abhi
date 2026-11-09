# Agent Core Verification Report (u4a–u4f)

## Comparison: pi `packages/agent/src/agent-loop.ts` vs Karta `karta/src/agent/agent.cpp`

---

### u4a: Event Wire Format Mapping

| pi Event | Karta Event | Match | Gaps |
|----------|-------------|-------|------|
| `agent_start` | `agent_start` | ✅ | |
| `agent_end` + `messages` | `agent_end` + `{}` | ❌ | Karta omits `messages` array |
| `turn_start` | `turn_start` | ✅ | |
| `turn_end` + `message` + `toolResults` | `turn_end` + `message` + `toolResults` | ✅ | Karta `toolResults` format differs |
| `message_start` + `message` | `message_start` + `message` | ✅ | |
| `message_update` + `message` + `assistantMessageEvent` | `message_update` + `{delta}` | ❌ | Karta sends only `delta` string, not full message + event |
| `message_end` + `message` | `message_end` + `message` | ✅ | |
| `tool_execution_start` + `toolCallId` + `toolName` + `args` | `tool_execution_start` + same | ✅ | |
| `tool_execution_update` + `toolCallId` + `toolName` + `args` + `partialResult` | **Missing** | ❌ | Karta has no partial tool result streaming |
| `tool_execution_end` + `toolCallId` + `toolName` + `result` + `isError` + `durationMs` | `tool_execution_end` + `toolCallId` + `toolName` + `isError` + `result` | ❌ | Karta missing `durationMs` |

### Karta Event Payload Differences
```cpp
// Karta turn_end payload
{"message": {...}, "toolResults": [{"role":"toolResult",...},...]}

// pi turn_end payload
{message: AgentMessage, toolResults: ToolResultMessage[]}

// Karta message_update
{"delta": "text..."}

// pi message_update
{message: AgentMessage, assistantMessageEvent: AssistantMessageEvent}
```

---

### u4b: Tool Call/Result Wire Format

| Aspect | pi | Karta | Match |
|--------|-----|-------|-------|
| Tool call block type | `toolCall` | `toolCall` | ✅ |
| Tool call fields | `id`, `name`, `arguments` | `id`, `name`, `arguments` | ✅ |
| Arguments format | JSON object | JSON string or object | ⚠️ Karta accepts both |
| Tool result message | `role: "toolResult"` | `role: "toolResult"` | ✅ |
| Tool result fields | `toolCallId`, `toolName`, `content[]`, `details`, `usage`, `isError`, `terminate`, `durationMs`, `timestamp` | `toolCallId`, `toolName`, `text`, `isError` | ❌ Karta simplified |
| Error format | `content: [{type:"text", text}]` | `text` string | ❌ Different structure |

---

### u4c: Tool Execution Modes (Sequential vs Parallel)

| Feature | pi | Karta | Status |
|---------|-----|-------|--------|
| `toolExecution: "sequential" \| "parallel"` | ✅ Configurable | ❌ Not implemented | **Missing** |
| Per-tool `executionMode` override | ✅ | ❌ | **Missing** |
| Sequential: prepare→execute→finalize each tool | ✅ | ✅ (implicit) | ✅ |
| Parallel: prepare all, execute concurrent, emit in completion order | ✅ | ❌ | **Missing** |
| `tool_execution_update` during parallel | ✅ | ❌ | **Missing** |
| Batch `terminate` logic | `shouldTerminateToolBatch()` checks all `result.terminate` | Checks `outcome.terminate` per tool | ⚠️ Partial |

**pi parallel flow** (lines 596–670):
1. Prepare all tool calls sequentially → `PreparedToolCall` or immediate
2. Immediate results → emit `tool_execution_end` immediately
3. Prepared calls → wrap in async functions
4. `Promise.all` executes concurrently
5. Results emitted in completion order (`tool_execution_end`)
6. Tool-result messages emitted later in assistant source order

**Karta flow** (lines 214–272): Always sequential, no concurrency.

---

### u4d: beforeToolCall / afterToolCall Hooks

| Hook | pi | Karta | Status |
|------|-----|-------|--------|
| `beforeToolCall(context, signal)` | ✅ Full context: `assistantMessage`, `toolCall`, `args`, `context` | ❌ Not implemented | **Missing** |
| `beforeToolCall` returns `{block, reason, terminate}` | ✅ | ❌ | **Missing** |
| Block → emit error tool result | ✅ | ❌ | **Missing** |
| `afterToolCall(context, signal)` | ✅ Full context: `assistantMessage`, `toolCall`, `args`, `result`, `isError`, `context` | ❌ Not implemented | **Missing** |
| `afterToolCall` returns partial override | ✅ `content`, `details`, `structuredContent`, `isError`, `usage`, `terminate` | ❌ | **Missing** |
| Merge semantics (field-by-field) | ✅ | ❌ | **Missing** |

---

### u4e: finishTurn / prepareNextTurn

| Hook | pi | Karta | Status |
|------|-----|-------|--------|
| `finishTurn(turnContext, signal)` | ✅ Returns `{action:"continue"}` \| `{action:"end"}` | ❌ Not implemented | **Missing** |
| `prepareRequest(requestContext, signal)` | ✅ Modify model, thinkingLevel, context before each request | ❌ Not implemented | **Missing** |
| `prepareNextTurn(turnContext)` | ✅ Returns `{context?, messages?, model?, thinkingLevel?}` | ❌ Not implemented | **Missing** |
| `transformContext(messages, signal)` | ✅ Context window management, pruning | ❌ Karta uses internal `maybe_compact()` | **Different approach** |

**Karta compaction** (lines 305–351):
- Internal, threshold-based
- Uses LLM to summarize transcript
- Replaces messages with single summary message

**pi approach**: `transformContext` hook for pruning, `prepareNextTurn` for turn transitions.

---

### u4f: Session Resume and Fork Behavior

| Feature | pi | Karta | Status |
|---------|-----|-------|--------|
| Resume via `agentLoopContinue` | ✅ Requires last message ≠ assistant | ✅ Via `-c` / `--session-id` | ✅ |
| Parent chain preservation | ✅ `parent_id` on each entry | ✅ `parent_id` in JSONL | ✅ |
| Fork session (`--fork`) | ✅ Copy to new cwd, new id | ✅ CLI option implemented | ✅ |
| Steering queue on resume | ✅ `getSteeringMessages()` | ✅ `queue_steering()` / `drain_steering()` | ✅ |
| Follow-up queue on resume | ✅ `getFollowUpMessages()` | ✅ `queue_followup()` / `drain_followups()` | ✅ |
| Turn counting on resume | ✅ Continues from context | ❌ Karta resets `turn_count_` | **Gap** |

---

## Summary: u4a–u4f Verification Results

| UUID | Task | Status | Priority |
|------|------|--------|----------|
| **u4a** | Event wire format 1:1 mapping | ❌ **FAIL** — missing `message_update` detail, `tool_execution_update`, `durationMs`, `agent_end.messages` | High |
| **u4b** | Tool call/result wire format | ❌ **FAIL** — Karta simplified result structure | High |
| **u4c** | Tool execution modes (parallel/sequential) | ❌ **FAIL** — parallel mode missing | Medium |
| **u4d** | beforeToolCall/afterToolCall hooks | ❌ **FAIL** — completely missing | Medium |
| **u4e** | finishTurn/prepareNextTurn/prepareRequest | ❌ **FAIL** — completely missing | Medium |
| **u4f** | Session resume/fork behavior | ⚠️ **PARTIAL** — turn count not preserved | Low |

---

## Required Fixes for Parity

### High Priority (Wire Format)
1. **Add `message_update` with full `AssistantMessageEvent`** — Karta emits only `delta`
2. **Add `tool_execution_update`** — for streaming partial tool results
3. **Add `durationMs` to `tool_execution_end`** — measure with monotonic clock
4. **Include `messages` in `agent_end`** — return produced messages
5. **Align tool result structure** — add `content[]`, `details`, `usage`, `terminate`, `timestamp`

### Medium Priority (Hooks & Modes)
6. **Implement `toolExecution` config** — `"sequential" | "parallel"` + per-tool override
7. **Add `beforeToolCall` hook** — with block/terminate support
8. **Add `afterToolCall` hook** — with merge semantics
9. **Add `finishTurn` hook** — for custom turn control
10. **Add `prepareRequest` hook** — for dynamic model/thinking changes
11. **Add `prepareNextTurn` hook** — for turn transition logic

### Low Priority
12. **Preserve `turn_count_` on session resume** — load from session

---

## Effort Estimate

| Fix | Effort | Risk |
|-----|--------|------|
| Wire format events (1–5) | 1–2 days | Low — additive only |
| Parallel execution (6) | 2–3 days | Medium — concurrency, ordering |
| Hook system (7–11) | 3–4 days | Medium — API design, backward compat |
| Turn count persistence (12) | 0.5 day | Low |

**Total**: ~1–2 weeks for full parity.

---

## Decision: Target Scope

**Recommendation**: Fix **wire format (u4a, u4b)** first — required for TUI/json/rpc compatibility and session fidelity. Defer hooks (u4d, u4e) and parallel execution (u4c) unless needed by consumers.

**Immediate action items**:
1. Update `AgentEvent` payloads to match pi exactly
2. Add `tool_execution_update` event type
3. Add `durationMs` timing in `execute_tool_calls`
4. Fix `agent_end` to include messages array
5. Align tool result message structure