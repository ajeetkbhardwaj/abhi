# Remaining pi Packages — Importance for Agents

## Overview
These 5 packages are **not ported** to Karta but are **core to pi's agent capabilities**. Each solves a fundamental problem for building reliable, production-grade coding agents.

---

## 1. Codemode (`packages/codemode`) — Editor Integration

### What It Does
- **Language Server Protocol (LSP) integration** — go-to-definition, find-references, hover, diagnostics
- **Editor-agnostic code intelligence** — works with VS Code, Neovim, Zed, etc.
- **Real-time code navigation** — agent can "read" code like a human: jump to definition, trace call chains
- **Semantic edits** — edit via AST, not text (rename symbol, extract method, organize imports)

### Why Critical for Agents
| Without Codemode | With Codemode |
|------------------|---------------|
| Agent reads files as plain text | Agent understands code structure |
| Grep/find for symbols | Go-to-definition, call hierarchy |
| Blind edits break references | Semantic rename updates all usages |
| No diagnostics access | Sees compiler errors, type mismatches |
| Can't navigate unknown codebases | Explores like senior developer |

### pi Implementation
```
packages/codemode/src/
├── lsp-client.ts       # LSP client (JSON-RPC)
├── lsp-server.ts       # Embedded LSP servers per language
├── symbols.ts          # Symbol index, cross-references
├── diagnostics.ts      # Live error/warning stream
├── edits.ts            # AST-based edit operations
└── language/
    ├── typescript.ts   # TS/JS via typescript-language-server
    ├── python.ts       # Python via pyright/pylsp
    ├── rust.ts         # Rust via rust-analyzer
    └── go.ts           # Go via gopls
```

### Karta Gap
- **No LSP client** — agent cannot navigate code semantically
- **No diagnostics** — agent doesn't see build errors until `bash` runs compiler
- **Text-only edits** — `edit` tool is fragile on large refactors

### Effort to Port: **High** (requires LSP client + language server management)

---

## 2. Durable (`packages/durable`) — Session Persistence & Recovery

### What It Does
- **Checkpointing** — save full agent state after every tool/turn
- **Time-travel debugging** — resume from any point, not just latest
- **Branch sessions** — explore multiple solutions from same context
- **Cross-machine transfer** — move session to another device
- **Crash recovery** — never lose work, even on power loss

### Why Critical for Agents
| Without Durable | With Durable |
|-----------------|--------------|
| Lose context on crash | Resume exactly where stopped |
| Single linear history | Branch: try fix A, then fix B from same point |
| Session tied to machine | Continue on laptop → desktop → server |
| No audit trail | Full replay for debugging/learning |
| Max 20 turns (Karta default) | Unlimited, checkpointed history |

### pi Implementation
```
packages/durable/src/
├── checkpoint.ts       # Periodic state snapshots
├── store.ts            # Persistent storage (SQLite/FS)
├── branch.ts           # Session forking with shared history
├── replay.ts           # Replay from any checkpoint
├── migrate.ts          # Schema upgrades
└── sync.ts             # Cross-device sync (uses chord)
```

### Karta Gap
- **Only JSONL v3 append** — no checkpoints, no branching
- **Crash = lost turn** — in-progress tool calls not persisted
- **No time-travel** — `-c` only resumes latest or by-id
- **Single device** — session files not portable by design

### Effort to Port: **Medium-High** (storage layer + checkpoint logic + branch DAG)

---

## 3. Chord (`packages/chord`) — Peer-to-Peer Agent Coordination

### What It Does
- **Multi-agent collaboration** — multiple agents work on same task
- **Shared session state** — real-time sync of messages, tools, context
- **Distributed tool execution** — run tools on remote machines
- **Conflict resolution** — merge concurrent edits, tool calls
- **NAT traversal** — direct P2P without central server

### Why Critical for Agents
| Without Chord | With Chord |
|---------------|------------|
| Single agent, single machine | Agent swarm across machines |
| Local tools only | Remote execution (CI, cloud, edge) |
| No collaboration | Pair-programming with AI agents |
| Sequential only | Parallel exploration of solutions |
| Centralized coordination | Decentralized, resilient |

### pi Implementation
```
packages/chord/src/
├── protocol.ts         # Chord wire protocol
├── peer.ts             # Peer discovery, connection
├── sync.ts             # CRDT-based state sync
├── transport/
│   ├── webrtc.ts       # Browser/Node WebRTC
│   ├── tcp.ts          # Direct TCP
│   └── relay.ts        # TURN/relay fallback
├── session.ts          # Shared session CRDT
└── tools.ts            # Distributed tool registry
```

### Karta Gap
- **No P2P layer** — single-process, single-machine only
- **No multi-agent** — cannot spawn sub-agents or collaborate
- **No remote execution** — all tools run locally

### Effort to Port: **Very High** (P2P networking + CRDTs + distributed systems)

---

## 4. Telemetry (`packages/telemetry`) — Observability & Debugging

### What It Does
- **Structured tracing** — spans for every agent event (LLM call, tool, turn)
- **Metrics** — token usage, latency, error rates, turn counts
- **Context propagation** — trace across async boundaries, threads
- **Export** — OpenTelemetry, Jaeger, Zipkin, Prometheus
- **In-memory store** — query traces without external deps

### Why Critical for Agents
| Without Telemetry | With Telemetry |
|-------------------|----------------|
| Blind to agent behavior | See every decision, tool, token |
| Can't optimize prompts | Measure token efficiency per task |
| Hard to debug failures | Trace exact failure path |
| No cost tracking | Per-session/project cost breakdown |
| No production monitoring | Alerts on error rates, latency |

### pi Implementation (Already Ported to Karta!)
```
Karta: karta/src/core/telemetry.cpp + karta/include/karta/telemetry.h
✅ TelemetrySpan, TelemetryContext, TelemetryCarrier
✅ TelemetryPropagation (W3C traceparent)
✅ TelemetryMetrics (counter, up-down-counter, histogram)
✅ InMemoryTelemetryContext + TelemetryStore
✅ Schema definition helper
```

### Karta Status: **✅ Ported (In-Memory Adapter)**
- Missing: **Exporters** (OTLP, Jaeger, Prometheus)
- Missing: **Sampling** (probabilistic, rate-limiting)
- Missing: **TUI integration** — no `/telemetry` command to view traces

### Effort to Complete: **Low-Medium** (exporters + TUI view)

---

## 5. Evals (`packages/evals`) — Automated Testing & Quality Gates

### What It Does
- **Test harness** — run agent on benchmark tasks
- **Assertion language** — check outputs, tool calls, final state
- **Regression detection** — compare against baseline runs
- **CI/CD integration** — gate merges on agent quality
- **Prompt optimization** — A/B test system prompts, models

### Why Critical for Agents
| Without Evals | With Evals |
|---------------|------------|
| "Vibes-based" development | Measurable quality improvement |
| Regressions shipped | Caught in CI before merge |
| No prompt tuning | Data-driven prompt engineering |
| Can't compare models | Objective model selection |
| Manual testing only | Automated nightly benchmarks |

### pi Implementation
```
packages/evals/src/
├── harness.ts          # Test runner, parallel execution
├── suite.ts            # Test suites, fixtures, setup/teardown
├── assertions.ts       # JSONPath, regex, semantic assertions
├── reporters.ts        # JUnit, JSON, HTML, console
├── baseline.ts         # Golden file management
├── optimize.ts         # Prompt/model A/B testing
└── benchmarks/
    ├── swe-bench.ts    # SWE-bench integration
    ├── humaneval.ts    # HumanEval coding tasks
    └── custom.ts       # Project-specific tasks
```

### Karta Gap
- **No test framework** — manual verification only
- **No regression detection** — changes untested
- **No benchmark integration** — can't run SWE-bench, HumanEval
- **No prompt optimization** — trial and error only

### Effort to Port: **Medium** (harness + assertions + CI integration)

---

## Priority Matrix for Karta

| Package | Agent Impact | Port Effort | Priority |
|---------|--------------|-------------|----------|
| **Codemode** | 🔴 Critical — semantic code understanding | High | **1** |
| **Durable** | 🟠 High — reliability, branching, recovery | Medium-High | **2** |
| **Evals** | 🟠 High — quality assurance, CI gate | Medium | **3** |
| **Telemetry** | 🟡 Medium — observability (core done) | Low-Medium | **4** |
| **Chord** | 🟢 Low-Med — multi-agent, distributed | Very High | **5** (later) |

---

## Recommended Next Steps

### Phase 1: Foundation (Do First)
1. **Codemode** — Add LSP client for at least TypeScript/Python
   - Enables: go-to-def, find-refs, diagnostics
   - Unblocks: reliable large-codebase navigation

2. **Durable Checkpoints** — Save state after each tool call
   - Enables: crash recovery, time-travel, branching
   - Storage: SQLite or append-only log

### Phase 2: Quality (Do Second)
3. **Evals Harness** — Run `swe-bench`, `humaneval` in CI
   - Enables: regression detection, model comparison
   - Integrate: GitHub Actions, `karta --mode json`

4. **Telemetry Exporters** — OTLP/Jaeger/Prometheus
   - Enables: production monitoring, cost tracking
   - TUI: `/telemetry` command to view traces

### Phase 3: Scale (Do Later)
5. **Chord** — Only if multi-agent/distributed needed
   - Massive effort, niche use case
   - Consider: external orchestration (Kubernetes, Temporal) instead

---

## Decision Required

**Which package to tackle first?** Recommend **Codemode** — highest agent impact, unblocks semantic code navigation that all other features amplify.

Or: **Durable** if reliability/recovery is the current pain point.