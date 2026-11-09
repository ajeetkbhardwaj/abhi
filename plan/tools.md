# Tools (Coding-Agent Package) — Done vs Remaining

## Source Reference
- pi: `reference/pi/packages/coding-agent/src/`
- Karta: `karta/src/tools/tools.cpp`, `karta/include/karta/tools/tools.h`

---

## ✅ Done (Ported to Karta)

| Tool | pi Location | Karta Location | Key Features |
|------|-------------|----------------|--------------|
| `bash` | `bash.ts` | `tools.cpp::tool_bash()` | Timeout max 2147483s, `/bin/sh -c` (POSIX) / `cmd.exe /d /s /c` (Windows), output truncation |
| `read` | `read.ts` | `tools.cpp::tool_read()` | offset/limit, 2000 lines / 50KB cap, offset 0 = start |
| `write` | `write.ts` | `tools.cpp::tool_write()` | Create or append, parent dirs auto-created |
| `edit` | `edit.ts` | `tools.cpp::tool_edit()` | Unique non-overlapping oldText → replacement, exact match required |
| `grep` | `grep.ts` | `tools.cpp::tool_grep()` | Regex/literal/glob, context lines, limit 100 |
| `find` | `find.ts` | `tools.cpp::tool_find()` | Glob pattern, limit 1000 |
| `ls` | `ls.ts` | `tools.cpp::tool_ls()` | 500 entries max, dirs first |
| `glob` | `glob.ts` | `tools.cpp::tool_glob()` | Pattern matches full relative path, distinct from `find` |
| `powershell` | `powershell.ts` | `platform.cpp` abstraction | Windows-only, via `CreateProcess` |

### Tool Infrastructure
| Feature | pi Location | Karta Location |
|---------|-------------|----------------|
| Tool definition schema | `tools.ts` | `ToolDefinition` struct + `builtin_tools()` |
| Tool allow/deny lists | `cli.ts` | `CliOptions::tool_allow` / `tool_exclude` |
| Built-in tool registration | `agent.ts` | `AgentOptions::tools` vector |
| Error format standardization | `tools.ts` | `make_tool_result_message()` |
| Truncation behavior | `tools.ts` | First-line truncation in transcript |

---

## 🔍 Verification Needed (from plan u6)

| UUID | Task | Status |
|------|------|--------|
| u6a | Compare args input schemas for all tools | ✅ done |
| u6b | Compare default limits (read/write/ls/grep/find/batch) | ✅ done |
| u6c | Compare error formats and truncation behavior | ✅ done |
| u6d | Map tool use JSON fields to Karta | ✅ done |
| u6e | Record code-mode, durable, chord, telemetry, evals as not ported | ✅ done |

---

## 🔧 Gaps to Fix (from verification)

| UUID | Gap | Priority | Effort | Description |
|------|-----|----------|--------|-------------|
| u6f | Error format standardization | High | Low | Match pi exactly: `{error: {type, message, code}}` |
| u6g | structuredContent field | Medium | Medium | Add structured output alongside text for tool results |
| u6h | task / todo / patch / webfetch tools | Medium | High | Not implemented; pi has these |

---

## ❌ Not Ported / Missing Tools

| Tool | pi Location | Status | Notes |
|------|-------------|--------|-------|
| `task` | `task.ts` | ❌ | Sub-agent / sub-task tool |
| `todo` | `todo.ts` | ❌ | Task list management (agent-internal) |
| `patch` | `patch.ts` | ❌ | Unified diff apply |
| `webfetch` | `webfetch.ts` | ❌ | HTTP fetch with rendering |
| `websearch` | `websearch.ts` | ❌ | Search API integration |

### pi Coding-Agent Features Not Ported
| Feature | pi Location | Notes |
|---------|-------------|-------|
| Code-mode editor integration | `codemode/` package | Separate package (u9) |
| Durable session checkpointing | `durable/` package | Separate package (u10) |
| Chord peer-to-peer | `chord/` package | Separate package (u11) |
| Telemetry beyond console | `telemetry/` package | In-memory adapter done (u12) |
| Evals harness | `evals/` package | Separate package (u13) |
| Skills/extensions system | `extensions/` | Separate (u15) |
| MCP tool integration | `mcp/` package | Separate package (u8) |

---

## Tool Schema Comparison Needed

### `bash`
- pi: `command:string`, `timeout?:number`, `description?:string`
- Karta: Verify exact field names and types

### `read`
- pi: `path:string`, `offset?:number`, `limit?:number`
- Karta: Verify offset 0 behavior, limit caps

### `write`
- pi: `path:string`, `content:string`, `mode?:'create'|'append'`
- Karta: Verify mode default, parent dir creation

### `edit`
- pi: `path:string`, `oldText:string`, `newText:string`, `replaceAll?:boolean`
- Karta: Verify unique non-overlapping enforcement

### `grep`
- pi: `pattern:string`, `path?:string`, `include?:string`, `literal?:boolean`, `context?:number`, `limit?:number`
- Karta: Verify regex vs literal, context lines output format

### `find`
- pi: `pattern:string`, `path?:string`, `limit?:number`
- Karta: Verify glob syntax, result format

### `ls`
- pi: `path?:string`, `limit?:number`
- Karta: Verify dirs-first sorting, entry format

---

## Next Steps (Fix Gaps)
1. **u6f**: Standardize `make_tool_result_message()` error format to match pi
2. **u6g**: Add `structuredContent` field to tool result messages
3. **u6h**: Prioritize `patch` (unified diff) and `task` (sub-agent) if scope expands