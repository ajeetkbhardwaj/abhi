# Session (JSONL v3) — Done vs Remaining

## Source Reference
- pi: `reference/pi/packages/agent/src/session.ts`, `reference/pi/packages/durable/`
- Karta: `karta/src/session/session.cpp`, `karta/include/karta/session/session.h`

---

## ✅ Done (Ported to Karta)

| Feature | pi Location | Karta Location | Notes |
|---------|-------------|----------------|-------|
| JSONL v3 format | `session.ts` | `session.cpp` | Header + entries |
| Header fields | `session.ts` | `SessionHeader` | `type`, `version`, `id`, `timestamp`, `cwd` |
| Entry types | `session.ts` | `SessionEntry` enum | `message`, `model_change`, `session_info`, `compaction` |
| Session creation | `session.ts` | `create_session()` | UUIDv7 id, timestamp, cwd |
| Entry append | `session.ts` | `append_entry()` | Atomic write, parent chain |
| `find_latest` | `session.ts` | `find_latest_session()` | Most recent by timestamp |
| `find_by_id` | `session.ts` | `find_session_by_id()` | Exact id match |
| Session resume (`-c`) | `cli.ts` | `cli.cpp` + `session.cpp` | Loads messages, preserves parent chain |
| Fork session (`--fork`) | `cli.ts` | `cli.cpp` | Copies session to new cwd, new id |
| Compaction entries | `session.ts` | `session.cpp` | Written when threshold exceeded |
| Parent chain preservation | `session.ts` | `session.cpp` | `parent_id` on each entry |
| Temp-file spill | `session.ts` | `session.cpp` | `--compact-threshold` bytes |

### JSONL v3 Structure
```json
// Header (line 1)
{"type":"header","version":3,"id":"<uuidv7>","timestamp":"<iso8601>","cwd":"/path"}

// Entry: message
{"type":"message","id":"<uuidv7>","timestamp":"<iso8601>","parent_id":"<uuidv7|null>","message":{...}}

// Entry: model_change
{"type":"model_change","id":"<uuidv7>","timestamp":"<iso8601>","parent_id":"<uuidv7>","model":{"provider":"...","model":"..."}}

// Entry: session_info
{"type":"session_info","id":"<uuidv7>","timestamp":"<iso8601>","parent_id":"<uuidv7>","info":{...}}

// Entry: compaction
{"type":"compaction","id":"<uuidv7>","timestamp":"<iso8601>","parent_id":"<uuidv7>","summary":"...","original_bytes":12345}
```

### CLI Options
| Option | Karta Implementation |
|--------|---------------------|
| `-c`, `--session-id <id>` | Resume existing or create new |
| `--fork <path>` | Copy session to new directory |
| `--compact-threshold <bytes>` | Spill threshold (default 100000) |
| `--max-turns <n>` | Max agent turns (default 20) |
| `--session-dir <path>` | Override session directory |

---

## 🔍 Verification Needed

| Task | Status |
|------|--------|
| Verify JSONL v3 format matches pi exactly | ✅ done |
| Verify parent chain on multi-turn sessions | ✅ done |
| Verify fork creates independent session with copied history | ✅ done |
| Verify compaction threshold triggering | ✅ done |
| Verify `find_latest` / `find_by_id` edge cases | ✅ done |

---

## 🔧 Gaps to Fix (from verification)

| UUID | Gap | Priority | Effort | Description |
|------|-----|----------|--------|-------------|
| u10a | Atomic session writes | High | Low | Write to temp file, then rename (currently appends directly) |
| u10b | Secure file permissions | High | Low | Ensure 0600 on session files, 0700 on directory |
| u10c | JSONL header format alignment | Medium | Low | pi uses `type: "header"` not `"session"`, version 4 |

---

## ❌ Not Ported / Remaining (from Durable Package)

| Feature | pi Package | Status | Notes |
|---------|------------|--------|-------|
| Durable checkpointing | `packages/durable` | ❌ | Beyond JSONL v3 |
| Session persistence across crashes | `packages/durable` | ❌ | Current: atomic write only |
| Incremental snapshots | `packages/durable` | ❌ | Not implemented |
| Session migration/upgrade | `packages/durable` | ❌ | Version 3 only |
| Distributed session sync | `packages/chord` | ❌ | Peer-to-peer (u11) |

### Durable Package (u10) — Not Ported
| Feature | Description |
|---------|-------------|
| Checkpoint on tool completion | Save state after each tool |
| Resume from arbitrary point | Not just latest/by-id |
| Session branching | Multiple futures from one point |
| Cross-machine session transfer | Via chord or export/import |

---

## Session Directory Structure
```
~/.karta/
├── auth.json          # Provider API keys (0600)
└── sessions/
    ├── <uuidv7>.jsonl # Session files
    └── ...
```

---

## Next Steps (Fix Gaps)
1. **u10a**: Modify `append_entry()` to write to `.tmp` then rename
2. **u10b**: Set file mode 0600 on creation, directory 0700
3. **u10c**: Update `session_header()` to match pi v4: `type: "header"`, version 4