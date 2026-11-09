# Provider Bridge Verification Report (u5a–u5e)

## Comparison: pi `packages/ai/src/api/` vs Karta `karta/src/ai/provider.cpp`

---

### u5a: OpenAI-Compatible SSE Event Mapping

| SSE Event Type | pi (openai-completions.ts) | Karta (stream_openai) | Match |
|----------------|----------------------------|----------------------|-------|
| `choices[0].delta.content` | Text delta | ✅ `delta["content"]` | ✅ |
| `choices[0].delta.reasoning_content` | Reasoning delta | ✅ `delta["reasoning_content"]` | ✅ |
| `choices[0].delta.reasoning` | Reasoning delta (alt) | ✅ `delta["reasoning"]` | ✅ |
| `choices[0].delta.thinking` | Thinking delta (alt) | ✅ `delta["thinking"]` | ✅ |
| `choices[0].delta.tool_calls` | Tool call streaming | ✅ `delta["tool_calls"]` | ✅ |
| `choices[0].finish_reason` | Stop reason | ✅ Maps to `stop|length|toolUse` | ✅ |
| `usage` in final chunk | Token counts | ✅ `chunk["usage"]` | ✅ |
| `usage.prompt_tokens_details.cached_tokens` | Cache read | ✅ | ✅ |
| `error` in chunk | Stream error | ✅ `chunk["error"]` | ✅ |
| `[DONE]` marker | Stream end | ✅ `data == "[DONE]"` | ✅ |

**Gaps in Karta:**
| Missing | pi Behavior | Karta Status |
|---------|-------------|--------------|
| `reasoning_details` array | OpenAI reasoning details (summary/encrypted/text) | ❌ Not parsed |
| `delta.role` | Assistant role in delta | ❌ Ignored |
| `logprobs` | Token probabilities | ❌ Not parsed |
| `choices[0].index` | Multi-choice (n>1) | ❌ Assumes index 0 |
| `service_tier` | OpenAI service tier | ❌ Ignored |
| `system_fingerprint` | Model fingerprint | ❌ Ignored |

---

### u5b: Anthropic Event Mapping

| SSE Event Type | pi (anthropic-messages.ts) | Karta (stream_anthropic) | Match |
|----------------|----------------------------|--------------------------|-------|
| `message_start` | Initial usage | ✅ `type == "message_start"` | ✅ |
| `content_block_start` (text) | Text block start | ❌ **Missing** — Karta skips | ❌ |
| `content_block_start` (tool_use) | Tool call start | ✅ `block["type"] == "tool_use"` | ✅ |
| `content_block_delta` (text_delta) | Text streaming | ✅ `delta["type"] == "text_delta"` | ✅ |
| `content_block_delta` (thinking_delta) | Thinking streaming | ✅ `delta["type"] == "thinking_delta"` | ✅ |
| `content_block_delta` (input_json_delta) | Tool args streaming | ✅ `delta["type"] == "input_json_delta"` | ✅ |
| `content_block_delta` (signature_delta) | Signature streaming | ❌ Not handled | ❌ |
| `message_delta` + `stop_reason` | Stop reason | ✅ Maps `tool_use|max_tokens|end_turn|stop_sequence` | ✅ |
| `message_delta` + `usage` | Output tokens | ✅ | ✅ |
| `error` | Stream error | ✅ `type == "error"` | ✅ |

**Gaps in Karta:**
| Missing | pi Behavior | Karta Status |
|---------|-------------|--------------|
| `content_block_start` (text) | Creates text block, merges consecutive | ❌ Karta assumes text starts implicitly |
| `content_block_delta` (signature_delta) | Beta: reasoning signature | ❌ Not parsed |
| `message_stop` | Explicit end marker | ❌ Karta infers from finish |
| `ping` events | Keepalive | ❌ Ignored (harmless) |
| Cache control headers | `anthropic-beta` headers | ❌ Not used |

---

### u5c: NVIDIA Endpoint & Auth Mapping

| Aspect | pi (nvidia.ts) | Karta | Match |
|--------|----------------|-------|-------|
| Provider ID | `"nvidia"` | `"nvidia"` | ✅ |
| Base URL | `https://integrate.api.nvidia.com/v1` | Same | ✅ |
| Auth env var | `NVIDIA_API_KEY` | Same | ✅ |
| API type | OpenAI Completions | OpenAI-compatible | ✅ |
| Models | `NVIDIA_MODELS` from catalog | Hardcoded default | ⚠️ No model catalog |

**Karta NVIDIA gaps:**
- No model catalog loading (pi has `nvidia.models.ts` with 20+ models)
- No model metadata (context window, pricing, capabilities)
- Uses generic OpenAI-compatible streaming (correct for NVIDIA)

---

### u5d: Raw Provider Content Types

| Content Type | pi Support | Karta Support |
|--------------|------------|---------------|
| Text | ✅ | ✅ |
| Thinking/Reasoning | ✅ (block type `thinking`) | ✅ (via `[thinking]` tags) |
| Tool Calls | ✅ (block type `toolCall`/`tool_use`) | ✅ |
| Images (input) | ✅ `ImageContent` block | ❌ Not implemented |
| Images (output) | ✅ `ImageContent` in response | ❌ Not implemented |
| Audio (input) | ✅ `AudioContent` block | ❌ Not implemented |
| Audio (output) | ❌ (OpenAI Realtime only) | ❌ |
| Cache Control | ✅ `cache_control` on blocks | ❌ Not implemented |
| Citations | ✅ `citations` on tool results | ❌ Not implemented |

**Karta message format** (simplified):
```json
{"role":"assistant","content":[{"type":"text","text":"..."},{"type":"toolCall",...}]}
```

**pi message format** (richer):
```json
{"role":"assistant","content":[{"type":"text",...},{"type":"thinking",...},{"type":"toolCall",...},{"type":"image",...}]}
```

---

### u5e: Streaming Partial Results & Idempotency

| Aspect | pi | Karta | Match |
|--------|-----|-------|-------|
| Partial text streaming | ✅ `on_text` callback per delta | ✅ `on_text` per delta | ✅ |
| Partial thinking streaming | ✅ `thinking_delta` | ✅ `[thinking]` tags | ✅ |
| Partial tool call args | ✅ `input_json_delta` / `tool_calls` delta | ✅ Accumulates `arguments` | ✅ |
| Partial tool results | ✅ `onUpdate` callback in `execute()` | ❌ **Missing** — no tool streaming | ❌ |
| Idempotency keys | ✅ Provider-specific (OpenAI `request_id`) | ❌ Not implemented | ❌ |
| Request retry with same key | ✅ Built into provider clients | ❌ No retry logic | ❌ |
| Deduplication on retry | ✅ Server-side | ❌ Client has no support | ❌ |

**pi idempotency** (from `openai-completions.ts`):
- OpenAI: `X-Request-ID` header for deduplication
- Anthropic: `anthropic-request-id` header
- Retry logic in `retryProviderRequest()` with exponential backoff

**Karta**: No retry, no idempotency headers, single attempt.

---

## Provider Coverage Matrix

| Provider | pi Status | Karta Status | Effort to Add |
|----------|-----------|--------------|---------------|
| **anthropic** | ✅ Full (Messages API) | ✅ Full | — |
| **openai** | ✅ Responses + Completions | ✅ Completions only | Low (add Responses) |
| **nvidia** | ✅ OpenAI Completions | ✅ OpenAI Completions | — |
| **openrouter** | ✅ OpenAI Compat + cost | ⚠️ Config only, no streaming test | Low |
| **groq** | ✅ OpenAI Compat | ⚠️ Config only | Low |
| **deepseek** | ✅ OpenAI Compat | ⚠️ Config only | Low |
| **xai** | ✅ OpenAI Compat | ⚠️ Config only | Low |
| **mistral** | ✅ OpenAI Compat | ⚠️ Config only | Low |
| **ollama** | ✅ OpenAI Compat | ⚠️ Config only | Low |
| **lmstudio** | ✅ OpenAI Compat | ⚠️ Config only | Low |
| **google/gemini** | ✅ Native API | ❌ Not implemented | Medium |
| **vertex** | ✅ Native API | ❌ Not implemented | Medium |
| **bedrock** | ✅ Converse Stream | ❌ Not implemented | High |
| **cohere** | ✅ Native API | ❌ Not implemented | Medium |
| **azure openai** | ✅ OpenAI Compat | ❌ Not implemented | Low |
| **github copilot** | ✅ OAuth + API | ❌ Not implemented | High |

---

## Summary: u5a–u5e Verification Results

| UUID | Task | Status | Priority |
|------|------|--------|----------|
| **u5a** | OpenAI SSE event mapping | ⚠️ **PARTIAL** — core events work, missing reasoning_details, logprobs, multi-choice | Medium |
| **u5b** | Anthropic event mapping | ⚠️ **PARTIAL** — missing content_block_start(text), signature_delta, message_stop | Medium |
| **u5c** | NVIDIA endpoint/auth | ✅ **PASS** — correct endpoint, auth, API type | — |
| **u5d** | Raw content types | ❌ **FAIL** — no images, audio, cache control, citations | Low (future) |
| **u5e** | Partial results/idempotency | ❌ **FAIL** — no tool streaming, no retry, no idempotency | Medium |

---

## Required Fixes for Parity

### High Priority (Streaming Correctness)
1. **Anthropic `content_block_start` for text** — merge consecutive text blocks like pi
2. **OpenAI `reasoning_details` parsing** — handle summary/encrypted/text details
3. **Add request retry logic** — exponential backoff with idempotency headers
4. **Add `X-Request-ID` / `anthropic-request-id`** — for deduplication

### Medium Priority (Feature Parity)
5. **OpenAI Responses API support** — pi uses Responses API for OpenAI
6. **Add 7 missing OpenAI-compat providers** — openrouter, groq, deepseek, xai, mistral, ollama, lmstudio (config + streaming test)
7. **NVIDIA model catalog** — load from pi's `nvidia.models.ts`

### Low Priority (Advanced)
8. **Image input/output support** — `ImageContent` blocks
9. **Cache control headers** — `cache_control` on blocks
10. **Tool result streaming** — `onUpdate` callback for partial tool results

---

## Effort Estimate

| Fix | Effort | Risk |
|-----|--------|------|
| Anthropic text block start (1) | 0.5 day | Low |
| OpenAI reasoning_details (2) | 1 day | Low |
| Retry + idempotency (3,4) | 2 days | Medium |
| Responses API (5) | 2 days | Medium |
| 7 providers config (6) | 1 day | Low (mostly config) |
| NVIDIA catalog (7) | 0.5 day | Low |
| Image support (8) | 3 days | Medium |
| Tool streaming (10) | 2 days | High |

**Total for high/medium**: ~1 week

---

## Decision: Target Scope

**Recommendation**: Fix **Anthropic text block start + OpenAI reasoning_details** first — these affect streaming fidelity. Add **7 provider configs** (trivial, just table entries + smoke test). Defer retry/idempotency unless reliability issues arise.

**Immediate action items**:
1. Fix `stream_anthropic` to handle `content_block_start` with `type: "text"`
2. Parse `reasoning_details` in `stream_openai`
3. Add provider table entries for: openrouter, groq, deepseek, xai, mistral, ollama, lmstudio
4. Add NVIDIA model list from pi catalog