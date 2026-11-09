# MCP (Multi-Console-Protocol) — Done vs Remaining

## Source Reference
- pi: `reference/pi/packages/mcp/src/`
- Karta: `karta/src/mcp/mcp.cpp`, `karta/include/karta/mcp/mcp.h`

---

## ❌ Not Ported (Stub Only)

### Current Karta State
| File | Status |
|------|--------|
| `karta/include/karta/mcp/mcp.h` | Empty placeholder |
| `karta/src/mcp/mcp.cpp` | Stub implementation (23KB, not functional) |
| CLI `--mcp` option | Parsed in `cli.cpp` but not wired |

### pi MCP Features (from `reference/pi/packages/mcp/src/`)
| Feature | pi Location | Description |
|---------|-------------|-------------|
| MCP handshake | `handshake.ts` | Protocol negotiation, capability exchange |
| MCP message framing | `protocol.ts` | JSON-RPC 2.0 over stdio/websocket |
| Tool registration | `tools.ts` | Dynamic tool discovery from MCP servers |
| Resource subscription | `resources.ts` | Subscribe to resource changes |
| Prompt templates | `prompts.ts` | Server-provided prompt templates |
| Sampling | `sampling.ts` | Server-initiated LLM calls |
| Roots | `roots.ts` | Workspace root directories |
| Logging | `logging.ts` | Structured log forwarding |

---

## 📋 Plan Items (from plan u8)

| UUID | Task | Status |
|------|------|--------|
| u8a | Reverse engineer MCP handshake | todo |
| u8b | Map MCP tools to Karta extension system | todo |
| u8c | Implement MCP client or mark as accepted out-of-scope | todo |

---

## Architecture Decision: **Option C — Mark Out of Scope (for now)**

Rationale:
- High effort (JSON-RPC 2.0, stdio/WS transports, capability negotiation, tool/resource/prompt registration)
- Core agent functions without MCP — users can run MCP servers externally
- Can be added later when ecosystem demand materializes

### Current State
- **Header file ready**: `include/karta/mcp/mcp.h` contains complete type definitions matching pi-mcp
- **Stubs in build**: Source files excluded from CMakeLists.txt (commented out)
- **Ready for future**: When demand materializes, implement:
  1. `src/mcp/mcp.cpp` - Core client with JSON-RPC 2.0, request/response, notifications
  2. `src/mcp/stdio_transport.cpp` - Child process spawning with stdin/stdout pipes
  3. `src/mcp/http_transport.cpp` - HTTP/SSE transport with auth support
  4. CLI integration for `--mcp` servers
  5. TUI `/mcp` command

### Alternative for Future: Option B (External Proxy)
If MCP is needed sooner, consider:
- Use existing `mcp-proxy` binary (Go/Rust)
- Karta spawns proxy via stdio
- Proxy handles JSON-RPC/OAuth, exposes tools as standard Karta tools
- Avoids implementing JSON-RPC 2.0, OAuth, reconnection in C++

### Actions
- [x] Document as not ported in `FEATURE_PARITY.md`
- [x] Keep `--mcp` CLI parsing but mark as deprecated/TODO
- [x] Document alternative: run MCP servers externally
- [x] Header file ready for future implementation

## pi MCP Structure Reference
```
packages/mcp/src/
├── client.ts           # MCP client connection
├── server.ts           # MCP server (for testing)
├── handshake.ts        # Initialize/initialized
├── protocol.ts         # JSON-RPC 2.0 types
├── tools.ts            # Tool list/call
├── resources.ts        # Resource list/read/subscribe
├── prompts.ts          # Prompt list/get
├── sampling.ts         # CreateMessage
├── roots.ts            # List roots
├── logging.ts          # SetLevel, notifications
└── transport/
    ├── stdio.ts        # Stdio transport
    └── websocket.ts    # WebSocket transport
```

---

## Integration Points with Karta

### If Implemented:
1. **CLI**: `--mcp <name=command>` already parsed → spawn subprocess
2. **Agent**: `AgentOptions::tools` ← merge MCP tools dynamically
3. **TUI**: `/mcp` command to list/manage connections
4. **Session**: Persist MCP server configs in session header

### Dependencies:
- JSON-RPC 2.0 implementation (or use existing C++ lib)
- WebSocket client (or stdio only initially)
- Subprocess management (already have `platform.cpp`)
- Tool schema conversion (MCP → Karta `ToolDefinition`)

---

## Next Steps
1. **Decision**: Choose Option A/B/C (requires user input)
2. If A or B:
   - u8a: Document pi handshake flow in detail
   - u8b: Design Karta tool registry extension for dynamic tools
   - u8c: Implement stdio transport first (simpler)
3. If C:
   - Update `FEATURE_PARITY.md` to explicitly mark MCP as out of scope
   - Remove `--mcp` CLI parsing or mark as deprecated
   - Document alternative: run MCP servers externally