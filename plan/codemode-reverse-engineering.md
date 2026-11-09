# Codemode Package — Reverse Engineering for Karta Port

## Overview

Codemode is a **JavaScript sandbox** that runs user scripts in an isolated QuickJS VM with:
- **Tool calling** via `tools.<name>(args)` 
- **Global functions** via top-level identifiers
- **Output helpers**: `text()`, `image()`, `exit()`, `console.*`
- **Key-value store**: `store(key, value)`, `load(key)` (synchronous, JSON)
- **Deterministic replay**: Same script + same seed + same results = identical execution
- **Multiple launchers**: Worker thread (Node/Bun), Inline (calling thread), Remote (RPC to Durable Object)

## Core Types (from `types.ts`)

### Tool Interface
```typescript
interface CodemodeTool {
  name: string;                           // Script calls: tools.<name>(args) and tools["<name>"](args)
  description?: string;                   // Doc comment in renderDeclarations, listed in ALL_TOOLS
  inputSchema?: CodemodeJsonSchema;       // Parameter type; unknown when omitted
  outputSchema?: CodemodeJsonSchema;      // Promise return type; unknown when omitted
  spread?: boolean;                       // Globals only: args as array instead of first arg
  signature?: string;                     // TS param list + return type for renderDeclarations
  execute(args: unknown, context): Promise<unknown> | unknown;
}
```

### Output Items
```typescript
type CodemodeOutputItem =
  | { type: "text"; text: string; console?: true }
  | { type: "image"; data: string; mimeType: string };  // base64
```

### Execution Result
```typescript
type CodemodeResult =
  | { ok: true; value; output: CodemodeOutputItem[]; calls: CodemodeCall[]; storeWrites: CodemodeStoreWrites; during? }
  | { ok: false; error: CodemodeError; output: CodemodeOutputItem[]; calls: CodemodeCall[]; during? };

type CodemodeErrorKind = "script" | "timeout" | "aborted" | "sandbox";
```

### Sandbox Options
```typescript
interface CodemodeSandboxBaseOptions {
  tools?: CodemodeTool[];
  globals?: CodemodeTool[];      // Top-level identifiers, not on tools
  timeoutMs?: number;            // Default 300000ms
  memoryLimitBytes?: number;     // QuickJS VM memory limit
}

interface CodemodeSandboxOptions extends CodemodeSandboxBaseOptions {
  wasm?: CodemodeWasmModule;
  workerUrl?: string | URL;
  acquireWorker?(): () => void;   // Concurrency control
  onWorkerStarted?(exited: Promise<void>): void;
}
```

### Deterministic Replay (`start()`)
```typescript
interface CodemodeRunOptions {
  startedAt: number;              // VM clock start (ms since epoch)
  seed: number;                   // 32-bit unsigned, seeds Math.random()
  tools?: CodemodeDeclaredTool[]; // Declared tools (with jsName for stable identifiers)
  globals?: CodemodeDeclaredGlobal[];
  store?: Record<string, unknown>;
  signal?: AbortSignal;
  timeoutMs?: number;
  onStarted?(fingerprint: string): void;
  onCall(call: CodemodeHostCall): void;  // Host MUST answer with deliver()
  onIdle?(seq: number, issued: number): void;
}
```

## Source Format (from `source.ts`)

```javascript
// @options: {"max_output_tokens": 2000, "timeout_ms": 30000}
const text = await tools.read({ path: "package.json" });
text(JSON.parse(text).name);
```

- Optional first line: `// @options: {JSON}`
- Supported fields: `max_output_tokens`, `timeout_ms`
- Line numbers preserved (options line replaced with empty line)

## Architecture

### Three-Launcher Model

| Launcher | VM Location | Use Case | Interrupt Model |
|----------|-------------|----------|-----------------|
| `CodemodeSandbox` | Node/Bun worker thread | Default, CPU isolation | Deadline timer + abort signal |
| `InlineCodemodeSandbox` | Calling thread | Cloudflare Workers, browsers, no worker threads | **Interrupt budget** (polls) |
| `RemoteCodemodeSandbox` | Remote isolate (Durable Object) | Keep responsive thread free | Remote interrupt budget |

### Message Protocol (from `protocol.ts`)

**Worker → Host:**
```typescript
type WorkerToHostMessage =
  | { type: "call"; id: number; target: "tool"|"global"; name: string; args: string|undefined }
  | { type: "output"; item: CodemodeOutputItem }
  | { type: "done"; ok: true; value: string|undefined; writes: string; during? }
  | { type: "done"; ok: false; error: ScriptErrorJson; during? }
  | { type: "crash"; message: string }
  | { type: "started"; fingerprint: string }
  | { type: "idle"; seq: number; issued: number };
```

**Host → Worker:**
```typescript
type HostToWorkerMessage = {
  type: "result"; id: number; ok: boolean; payload: string|undefined;
  seq?: number; now?: number; properties?: string;  // deterministic mode
};
```

### VM Bridge (from `vm.ts` + `prelude-source.ts`)

The VM runs a **prelude** that:
1. **Locks down built-ins** — freezes all built-in prototypes, prevents prototype pollution
2. **Builds API** — `tools`, `ALL_TOOLS`, `console`, `text()`, `image()`, `exit()`, `store()`, `load()`
3. **Bridges calls** — `bridge(kind, id, name, argsJson)` → host executes tool, returns result via `settle(id, ok, payload, propertiesJson)`

### Deterministic Mode

When `deterministic: { startedAt, seed }` is provided:
- **Time**: WASI clock reads virtual time (`startedAt` + `now` of processed result)
- **Randomness**: `random_get` draws from mulberry32 PRNG seeded from `seed`
- **Timezone**: UTC
- **GC**: `WeakRef`/`FinalizationRegistry` unavailable
- **Delivery order**: Results carry `seq`, applied strictly in order (buffers early ones)
- **Fingerprint**: Reports `started` with engine versions, prelude hash, limits, budget

## Key Implementation Details

### Tool Execution Flow
```
Script: tools.read({ path: "x" })
    ↓
Prelude: bridge("call", id, "read", JSON.stringify({path:"x"}))
    ↓
Host: receives "call" → executes tool → delivers result
    ↓
Prelude: settle(id, ok, payload, propertiesJson) → resolves promise
```

### Output Limits (from `prelude-source.ts`)
```typescript
MAX_OUTPUT_CHARS = 16 * 1024 * 1024;  // 16MB
MAX_OUTPUT_ITEMS = 100_000;
MAX_STORE_VALUE_CHARS = 256 * 1024;   // 256KB per value
MAX_STORE_TOTAL_CHARS = 1024 * 1024;  // 1MB total
```

### Image Handling
- Accepts: data: URL, `{image_url: "..."}`, `{type:"image", data:base64, mimeType}`
- Validates: PNG, JPEG, GIF, WebP via magic bytes
- Rejects: HTTP(S) URLs, invalid base64, wrong formats

### Declarations Rendering (from `declarations.ts`)
```typescript
renderDeclarations({ tools, globals }) → TypeScript declarations
```
- Converts JSON Schema → TS types (`schemaToType`)
- Handles `$ref`, `enum`, `const`, `anyOf`, `oneOf`, `allOf`, arrays, objects
- Renders constraint tags: `@integer @minimum 1 @maximum 100 @default 20`

## Porting Requirements for Karta (C++17)

### Dependencies Needed
1. **QuickJS** — C library, compile to wasm or link statically
2. **WASI** — For syscalls (clock, random, fd_write)
3. **Thread pool** — For worker-thread launcher
4. **JSON Schema → C++ type mapping** — For declarations rendering

### Core Components to Implement

| Component | Effort | Notes |
|-----------|--------|-------|
| QuickJS VM wrapper | High | Embed quickjs, manage lifecycle |
| Prelude (JS) | Medium | Port from `prelude-source.ts` |
| Host-VM bridge | High | Message passing, tool execution |
| Deterministic mode | Medium | Virtual clock, PRNG, result ordering |
| Worker launcher | Medium | pthreads + message queue |
| Inline launcher | Medium | Interrupt budget polling |
| Remote launcher | Low | RPC client stub |
| Schema rendering | Medium | Port `declarations.ts` logic |

### Integration with Karta Agent

1. **Agent tool** — Add `codemode` tool that takes script + options
2. **Session store** — Persist `store` key-values across runs
3. **TUI** — Show codemode executions, output, calls
4. **MCP bridge** — Expose codemode tools to MCP servers

### Priority for Karta

| Phase | Feature |
|-------|---------|
| 1 | **Inline launcher** (no worker threads needed, simplest) |
| 2 | **Basic tools** (read, write, bash, grep, etc.) |
| 3 | **Worker launcher** (for CPU isolation) |
| 4 | **Deterministic replay** (`start()` / `deliver()`) |
| 5 | **Declarations rendering** (for LSP integration) |
| 6 | **Remote launcher** (Durable Object equivalent) |

## Test Coverage to Replicate

| Test File | Key Behaviors |
|-----------|---------------|
| `sandbox.test.ts` | Execute, start/deliver, tools, globals, store, errors, limits |
| `deterministic.test.ts` | Replay fidelity, fingerprint, virtual time, PRNG |
| `declarations.test.ts` | Schema→TS rendering, refs, constraints, tags |
| `inline.test.ts` | Interrupt budget, timeout vs budget, stack size |
| `worker-lifetime.test.ts` | Worker acquisition, release, concurrency |

## Decision Points

1. **QuickJS linkage**: Static link vs WASM? Static is faster, WASM is portable.
2. **Memory model**: Shared VM per execution vs pooled VMs? Per-execution is simpler.
3. **Interrupt budget**: Port inline launcher's poll counting exactly.
4. **Schema rendering**: Full TS types vs simplified? Full for LSP integration.

---

**Recommendation**: Start with **Inline launcher + basic tools** — minimal dependencies, runs on any C++17 platform with WebAssembly (QuickJS compiles to WASM easily). Worker launcher can follow for true parallelism.