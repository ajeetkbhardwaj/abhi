# Session Verification Report

## Comparison: pi JSONL Session Format vs Karta `karta/src/session/session.cpp`

---

### pi JSONL Session Format (from test fixtures)

```jsonl
{"type":"session","id":"ffae836b-9420-4060-ac13-7745215f90ff","timestamp":"2025-12-09T00:53:29.825Z","cwd":"/path","provider":"anthropic","modelId":"claude-opus-4-5","thinkingLevel":"off","branchedFrom":"..."}
{"type":"message","timestamp":"2025-12-08T22:41:05.306Z","message":{"role":"user","content":[{"type":"text","text":"..."}],"timestamp":1765233665292}}
{"type":"message","timestamp":"2025-12-08T22:41:09.389Z","message":{"role":"assistant","content":[{"type":"toolCall",...}],"api":"anthropic-messages","provider":"anthropic","model":"claude-opus-4-5","usage":{...},"stopReason":"toolUse","timestamp":1765233665294}}
{"type":"message","timestamp":"2025-12-08T22:41:09.394Z","message":{"role":"toolResult","toolCallId":"...","toolName":"read","content":[{"type":"text","text":"..."}],"isError":false,"timestamp":1765233665294}}
{"type":"thinking_level_change","timestamp":"2025-12-08T22:45:42.397Z","thinkingLevel":"minimal"}
```

**Key pi format features:**
- Header type: `"session"` (not `"header"`)
- Header includes: `provider`, `modelId`, `thinkingLevel`, `branchedFrom`
- Message entries have `"type": "message"` with embedded `"message"` object
- Messages include `timestamp` (milliseconds since epoch)
- Assistant messages include `api`, `provider`, `model`, `usage`, `stopReason`
- Tool results include `isError`, `timestamp`
- Separate `"thinking_level_change"` entry type

---

### Karta JSONL v3 Format (from `session.cpp`)

```jsonl
{"type":"header","version":3,"id":"...","timestamp":"...","cwd":"/path"}
{"type":"message","id":"...","timestamp":"...","parentId":"...","message":{...}}
{"type":"model_change","id":"...","timestamp":"...","parentId":"...","provider":"...","modelId":"..."}
{"type":"session_info","id":"...","timestamp":"...","parentId":"...","name":"..."}
{"type":"compaction","id":"...","timestamp":"...","parentId":"...","summary":"...","tokensBefore":12345}
```

---

### Format Comparison

| Aspect | pi (v4) | Karta (v3) | Match |
|--------|---------|------------|-------|
| **Header type** | `"session"` | `"header"` | ❌ Different |
| **Header fields** | `id`, `timestamp`, `cwd`, `provider`, `modelId`, `thinkingLevel`, `branchedFrom` | `type`, `version`, `id`, `timestamp`, `cwd` | ❌ Karta minimal |
| **Version** | Not in header (implied v4) | `version: 3` | ⚠️ Different |
| **Entry types** | `message`, `thinking_level_change` | `message`, `model_change`, `session_info`, `compaction` | ⚠️ Different |
| **Message wrapper** | `{"type":"message", "message":{...}}` | Same | ✅ |
| **Message timestamp** | ISO8601 + millisecond field | ISO8601 | ⚠️ pi has both |
| **Parent chain** | Not explicit in test file | `parentId` on every entry | ✅ Karta better |
| **Tool calls in assistant** | Inside `message.content[]` | Inside `message.content[]` | ✅ |
| **Usage in assistant** | Inside `message` object | Inside `message.usage` | ✅ |
| **Stop reason** | `message.stopReason` | `message.stopReason` | ✅ |
| **Thinking level** | Separate `thinking_level_change` entry | Not tracked separately | ❌ Missing |
| **Model change** | Not in test (assistant has `model`) | Separate `model_change` entry | ⚠️ Different |
| **Compaction** | Not in test | Separate `compaction` entry | ✅ Karta has |
| **Fork tracking** | `branchedFrom` in header | `parentSession` in header | ✅ Equivalent |

---

### Karta Session Implementation Verification

| Feature | Karta Implementation | Status |
|---------|---------------------|--------|
| **Create session** | `Session::create()` - generates UUIDv7, writes header | ✅ |
| **Load session** | `Session::load()` - parses JSONL, reconstructs messages | ✅ |
| **Find by ID** | `Session::find_by_id()` - scans directory for ID in filename | ✅ |
| **Find latest** | `Session::find_latest()` - scans by cwd + mtime | ✅ |
| **Append entry** | `append_entry()` - UUIDv7, parentId, timestamp, atomic append | ✅ |
| **Parent chain** | `last_entry_id_` tracked, set as `parentId` | ✅ |
| **Message append** | `append_message()` - wraps in entry, updates in-memory | ✅ |
| **Model change** | `append_model_change()` - separate entry type | ✅ |
| **Session info** | `append_session_info()` - for name | ✅ |
| **Compaction** | `append_compaction()` - summary + tokensBefore | ✅ |
| **Fork support** | `parentSession` in header via `explicit_id` + `parent_session` | ✅ |
| **Atomic writes** | Opens file in append mode, flushes | ⚠️ Not atomic (no temp+rename) |
| **Directory perms** | Uses `fs::create_directories` | ⚠️ No 0700 enforcement |
| **File perms** | Default ofstream perms | ⚠️ No 0600 enforcement |

---

### Gaps from pi Format

| Missing in Karta | pi Has | Impact |
|------------------|--------|--------|
| `provider` in header | ✅ | Model resolution on resume |
| `modelId` in header | ✅ | Model resolution on resume |
| `thinkingLevel` in header | ✅ | Thinking restoration |
| `thinking_level_change` entries | ✅ | Thinking history |
| `branchedFrom` in header | ✅ (parentSession) | Fork tracking |
| Atomic write (temp+rename) | ❌ | Crash safety |
| 0700 dir / 0600 file perms | ❌ | Security |

---

### Session Resume / Fork Verification

| Scenario | pi Behavior | Karta Behavior | Match |
|----------|-------------|----------------|-------|
| Resume latest (`-c`) | Load most recent by cwd | `find_latest(cwd)` | ✅ |
| Resume by ID (`-c <id>`) | Load exact ID | `find_by_id(id)` | ✅ |
| Fork (`--fork <path>`) | Copy session, new ID, `branchedFrom` | `create(..., parent_session=old_id)` | ✅ |
| Parent chain integrity | Implicit via file sequence | Explicit `parentId` per entry | ✅ Karta better |
| Turn count on resume | Continues from context | Resets `turn_count_` | ❌ Gap |

---

## Summary

| Task | Status | Priority |
|------|--------|----------|
| JSONL v3 format vs pi v4 | ❌ **FAIL** — different header type, missing fields | High |
| Parent chain | ✅ **PASS** — explicit `parentId` | — |
| Session resume (`-c`) | ✅ **PASS** | — |
| Session fork (`--fork`) | ✅ **PASS** | — |
| Compaction entries | ✅ **PASS** | — |
| Atomic write safety | ❌ **FAIL** — no temp+rename | Medium |
| Secure perms (0700/0600) | ❌ **FAIL** | Medium |
| Thinking level tracking | ❌ **FAIL** — no separate entries | Low |

---

## Required Fixes for Parity

### High Priority (Format Compatibility)
1. **Change header type to `"session"`** — match pi v4
2. **Add `provider`, `modelId`, `thinkingLevel` to header** — for model restoration
3. **Add `thinking_level_change` entry type** — track thinking history
4. **Change version to 4** — or keep 3 but document difference

### Medium Priority (Reliability)
5. **Atomic write via temp file + rename** — crash safety
6. **Enforce 0700 directory / 0600 file perms** — security (like auth.json)
7. **Preserve `turn_count_` on session resume** — accurate max-turns

### Low Priority (Nice to Have)
8. **Add `branchedFrom` to header** — explicit fork tracking
9. **Compact threshold config** — already via `--compact-threshold`

---

## Effort Estimate

| Fix | Effort | Risk |
|-----|--------|------|
| Header format changes (1-4) | 1 day | Low |
| Atomic write (5) | 0.5 day | Low |
| Secure perms (6) | 0.5 day | Low |
| Turn count persistence (7) | 0.5 day | Low |
| BranchedFrom (8) | 0.5 day | Low |

**Total**: ~3 days for full parity

---

## Decision: Target Scope

**Recommendation**: Fix **header format (1-4)** + **atomic write (5)** + **secure perms (6)** first — these affect compatibility and safety. Defer turn count persistence and branchedFrom.

**Immediate action items**:
1. Update `session_header()` to use `"session"` type, add provider/modelId/thinkingLevel
2. Add `thinking_level_change` entry type in `session.h`/`session.cpp`
3. Implement atomic write: write to `.tmp`, `fsync`, rename
4. Set directory 0700, file 0600 on POSIX
5. Bump version to 4 (or document v3 vs v4 difference)