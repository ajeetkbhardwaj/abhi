# Tools Verification Report (u6a–u6d)

## Comparison: pi `packages/coding-agent/src/core/tools/` vs Karta `karta/src/tools/tools.cpp`

---

### u6a: Args Input Schema Comparison

| Tool | pi Schema | Karta Schema | Match |
|------|-----------|--------------|-------|
| **bash** | `command:string, timeout?:number` | `command:string, timeout?:number` | ✅ |
| **read** | `path:string, offset?:number, limit?:number` | `path:string, offset?:number, limit?:number` | ✅ |
| **write** | `path:string, content:string` | `path:string, content:string` | ✅ |
| **edit** | `path:string, edits:Array<{oldText:string, newText:string}>` | `path:string, edits:Array<{oldText:string, newText:string}>` | ✅ |
| **grep** | `pattern:string, path?:string, glob?:string, ignoreCase?:boolean, literal?:boolean, context?:number, limit?:number` | Same | ✅ |
| **find** | `pattern:string, path?:string, limit?:number` | Same | ✅ |
| **ls** | `path?:string, limit?:number` | Same | ✅ |

**Differences:**
- pi `edit` has `prepareArguments` for legacy single-edit format (`oldText`/`newText` at top level) — Karta doesn't handle this
- pi `bash` has `commandPrefix`, `spawnHook`, `exposeSessionEnvironment` options — Karta doesn't
- pi `read` supports images (returns `ImageContent`) — Karta returns error for images
- pi `grep` has `literal` mode — Karta has `literal` parameter (✅)

---

### u6b: Default Limits Comparison

| Limit | pi Default | Karta Default | Match |
|-------|------------|---------------|-------|
| **bash output** | 2000 lines / 50KB | 2000 lines / 50KB | ✅ |
| **bash timeout** | 2,147,483,647 ms | 2,147,483 s | ✅ |
| **read offset** | 1-indexed, 1 = first line | Same | ✅ |
| **read limit** | No default (0 = all) | 0 = all | ✅ |
| **read truncation** | 2000 lines / 50KB | 2000 lines / 50KB | ✅ |
| **write** | No limit | No limit | ✅ |
| **edit** | No limit (unique matches) | Same | ✅ |
| **grep limit** | 100 matches | 100 matches | ✅ |
| **grep context** | 0 lines | 0 lines | ✅ |
| **find limit** | 1000 results | 1000 results | ✅ |
| **ls limit** | 500 entries | 500 entries | ✅ |

**Truncation constants:**
```typescript
// pi (truncate.ts)
export const DEFAULT_MAX_LINES = 2000;
export const DEFAULT_MAX_BYTES = 50 * 1024;

// Karta (tools.cpp:372)
const std::size_t kMaxLines = 2000;
const std::size_t kMaxBytes = 50 * 1024;
```

---

### u6c: Error Formats & Truncation Behavior

| Aspect | pi | Karta | Match |
|--------|-----|-------|-------|
| **Error result structure** | `{content:[{type:"text",text}], details, isError:true, structuredContent}` | `{text, is_error}` | ❌ Different |
| **Bash timeout error** | `"timeout:<seconds>"` message + shell exit info | `"[timeout: Xs]"` appended | ⚠️ Similar |
| **Bash truncation notice** | `"[Showing last X of line Y... Full output: path]"` | `"[50KB limit reached; full output: path]"` | ⚠️ Different format |
| **Read offset OOB** | `"Offset X is beyond end of file (Y lines total)"` | `"[offset X is past end of file (Y lines)]"` | ✅ Similar |
| **Read truncation notice** | `"[Showing lines X-Y of Z. Use offset=N to continue.]"` | `"[N lines shown; 50KB limit. Use offset=M to continue.]"` | ⚠️ Different format |
| **Edit unique match error** | `"oldText was not found in path"` | Same | ✅ |
| **Edit overlap error** | `"edits overlap; merge them into one edit"` | Same | ✅ |
| **Grep limit notice** | `"[100 matches limit reached. Use limit=200 for more]"` | Same | ✅ |
| **Find limit notice** | `"[1000 results limit reached. Use limit=2000 for more]"` | Same | ✅ |
| **LS limit notice** | `"[500 entries limit reached. Use limit=1000 for more]"` | Same | ✅ |

**Key difference**: pi returns rich `structuredContent` for programmatic callers; Karta returns simplified text only.

---

### u6d: Tool Use JSON Fields Mapping

| Field | pi (AgentMessage) | Karta (Json) | Match |
|-------|-------------------|--------------|-------|
| **Tool call ID** | `toolCall.id` | `call["id"]` | ✅ |
| **Tool name** | `toolCall.name` | `call["name"]` | ✅ |
| **Arguments** | `toolCall.arguments` (object) | `call["arguments"]` (string or object) | ⚠️ Karta parses both |
| **Tool result role** | `"toolResult"` | `"toolResult"` | ✅ |
| **Tool result call ID** | `toolResult.toolCallId` | `message["toolCallId"]` | ✅ |
| **Tool result name** | `toolResult.toolName` | `message["toolName"]` | ✅ |
| **Tool result content** | `toolResult.content[]` (TextContent/ImageContent) | `message["content"]` (text only) | ❌ Karta no images |
| **Tool result details** | `toolResult.details` | Not in Karta | ❌ |
| **Tool result usage** | `toolResult.usage` | Not in Karta | ❌ |
| **Tool result isError** | `toolResult.isError` | `message["isError"]` | ✅ |
| **Tool result terminate** | `toolResult.terminate` | `outcome.terminate` | ✅ |
| **Tool result timestamp** | `toolResult.timestamp` | `message["timestamp"]` | ✅ |

**pi tool result message:**
```json
{
  "role": "toolResult",
  "toolCallId": "call_123",
  "toolName": "bash",
  "content": [{"type": "text", "text": "output..."}],
  "details": {...},
  "usage": {...},
  "isError": false,
  "terminate": false,
  "timestamp": 1234567890
}
```

**Karta tool result message:**
```json
{
  "role": "toolResult",
  "toolCallId": "call_123",
  "toolName": "bash",
  "content": [{"type": "text", "text": "output..."}],
  "isError": false,
  "timestamp": 1234567890
}
```

---

## Missing Tools (pi has, Karta lacks)

| Tool | pi File | Description |
|------|---------|-------------|
| **glob** | `glob.ts` | Pattern matching only (no content search), distinct from `find` |
| **task** | `task.ts` | Sub-agent / sub-task delegation |
| **todo** | `todo.ts` | Task list management (agent-internal) |
| **patch** | `patch.ts` | Unified diff apply |
| **webfetch** | `webfetch.ts` | HTTP fetch with rendering |
| **websearch** | `websearch.ts` | Search API integration |

**Note**: pi's `glob` tool is for file pattern matching (like `find` but simpler, no recursion control). Karta's `find` covers similar ground but with glob patterns.

---

## Summary: u6a–u6d Verification Results

| UUID | Task | Status | Priority |
|------|------|--------|----------|
| **u6a** | Args input schemas | ✅ **PASS** — schemas match; minor: pi `prepareArguments` for legacy edit format | Low |
| **u6b** | Default limits | ✅ **PASS** — all limits match exactly | — |
| **u6c** | Error formats & truncation | ⚠️ **PARTIAL** — logic matches, message formats differ | Medium |
| **u6d** | Tool use JSON fields | ❌ **FAIL** — missing `details`, `usage`, `structuredContent`, image support | Medium |

---

## Required Fixes for Parity

### Medium Priority (Session Fidelity)
1. **Align tool result message structure** — add `details`, `usage`, `structuredContent` fields
2. **Standardize error message format** — match pi's truncation notices exactly
3. **Add `terminate` to tool result message** — for early termination signaling

### Low Priority (Nice to Have)
4. **Handle legacy edit format** — `prepareArguments` for single `oldText`/`newText`
5. **Image support in `read`** — return `ImageContent` blocks
6. **Bash `commandPrefix` / `spawnHook`** — for session env injection
7. **Structured output for bash** — `exit_code`, `wall_time_seconds`, `full_output_path`

---

## Effort Estimate

| Fix | Effort | Risk |
|-----|--------|------|
| Tool result structure alignment (1,3) | 1 day | Low |
| Error message format standardization (2) | 0.5 day | Low |
| Legacy edit format (4) | 0.5 day | Low |
| Image support (5) | 2 days | Medium |
| Bash extensions (6,7) | 1 day | Low |

**Total for medium**: ~1.5 days

---

## Decision: Target Scope

**Recommendation**: Fix **tool result message structure** first — required for session fidelity and agent loop correctness. Standardize **error message formats** for consistent UX. Defer image support and bash extensions.

**Immediate action items**:
1. Update `make_tool_result_message` to include `details`, `usage`, `structuredContent`, `terminate`
2. Update `execute_tool_calls` to populate these from `ToolOutcome`
3. Align truncation notice text to match pi exactly
4. Add `prepareArguments` handling for edit tool (legacy format)