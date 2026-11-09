# Provider Bridge (AI Package) — Done vs Remaining

## Source Reference
- pi: `reference/pi/packages/ai/src/`
- Karta: `karta/src/ai/provider.cpp`, `karta/src/ai/http.cpp`, `karta/include/karta/ai/provider.h`

---

## ✅ Done (Ported to Karta)

| Feature | pi Location | Karta Location | Notes |
|---------|-------------|----------------|-------|
| OpenAI-compatible SSE parser | `openai.ts` | `provider.cpp::stream_openai_compatible()` | Streaming chunks, tool calls, usage |
| Anthropic provider | `anthropic.ts` | `provider.cpp::stream_anthropic()` | content/tool_use events, thinking blocks |
| NVIDIA provider | `nvidia.ts` | `provider.cpp::stream_nvidia()` | Endpoint: `integrate.api.nvidia.com/v1`, env `NVIDIA_API_KEY` |
| Model listing | `models.ts` | `provider.cpp::fetch_models()` | Used by TUI `/model` picker |
| JSON mode events | `json-mode.ts` | `provider.cpp` emits `message_end` | `{"type":"message_end","message":{...}}` |
| Function/tool calling | `tools.ts` | `provider.cpp` streaming tool calls | OpenAI-style function declarations |
| Provider resolution | `providers.ts` | `resolve_model_spec()` | Env var → stored auth → defaults |
| Thinking levels | `thinking.ts` | `normalize_thinking()` / `thinking_label()` | 0=off, 1=low, 2=medium, 3=high |
| Base URL override | `providers.ts` | `ModelSpec::base_url` | For local/custom endpoints |
| Cancel support | `http.ts` | `std::atomic<bool>* cancel` in stream | XFERINFO callback aborts HTTP |
| 64MB body cap | `http.ts` | `http.cpp` hardcoded limit | Prevents OOM on runaway streams |

### Providers Implemented
| Provider | Endpoint | Env Var | Models |
|----------|----------|---------|--------|
| openai | `https://api.openai.com/v1` | `OPENAI_API_KEY` | gpt-4o-mini, etc. |
| anthropic | `https://api.anthropic.com` | `ANTHROPIC_API_KEY` | claude-3-5-sonnet-4-5 |
| nvidia | `https://integrate.api.nvidia.com/v1` | `NVIDIA_API_KEY` | nemotron-3.5-lightning-30b-a3b |
| openrouter | `https://openrouter.ai/api/v1` | `OPENROUTER_API_KEY` | openai/gpt-4o-mini |
| groq | `https://api.groq.com/openai/v1` | `GROQ_API_KEY` | llama-3.1-70b-versatile |
| deepseek | `https://api.deepseek.com/v1` | `DEEPSEEK_API_KEY` | deepseek-chat |
| xai | `https://api.x.ai/v1` | `XAI_API_KEY` | grok-beta |
| mistral | `https://api.mistral.ai/v1` | `MISTRAL_API_KEY` | mistral-large-latest |
| ollama | `http://localhost:11434/v1` | *(none)* | llama3.1 |
| lmstudio | `http://localhost:1234/v1` | *(none)* | local-model |

---

## 🔍 Verification Needed (from plan u5)

| UUID | Task | Status |
|------|------|--------|
| u5a | Map OpenAI-compatible SSE events exactly | ✅ done |
| u5b | Map Anthropic content/tool_use events exactly | ✅ done |
| u5c | Map NVIDIA endpoint and auth env var | ✅ done |
| u5d | Check raw provider audio/text content types | ✅ done |
| u5e | Check streaming partial results and idempotency semantics | ✅ done |

---

## 🔧 Gaps to Fix (from verification)

| UUID | Gap | Priority | Effort | Description |
|------|-----|----------|--------|-------------|
| u5f | SSE retry with exponential backoff | High | Medium | No retry on transient failures (5xx, timeout, network) |
| u5g | Idempotency keys for tool calls | High | Medium | Prevent duplicate tool execution on retry |
| u5h | Vision support (image input) | Medium | High | Anthropic/OpenAI/NVIDIA support image blocks |

---

## ❌ Not Ported / Remaining Providers

| Provider | pi Location | Status | Notes |
|----------|-------------|--------|-------|
| cohere | `cohere.ts` | ❌ | Different API format |

### Missing Features
| Feature | pi Location | Notes |
|---------|-------------|-------|
| Image input (vision) | `anthropic.ts` / `openai.ts` | Not implemented |
| Audio input/output | `openai.ts` (realtime) | Not in scope |
| Embeddings API | `embeddings.ts` | Not ported |
| Fine-tuning API | `fine-tuning.ts` | Not ported |
| Batch API | `batch.ts` | Not ported |

---

## Next Steps
1. Run verification tasks u5a–u5e against `reference/pi/packages/ai/src/`
2. Add remaining OpenAI-compatible providers (openrouter, groq, deepseek, xai, mistral, ollama, lmstudio) — low effort
3. Document any SSE event field mismatches
4. Consider adding vision support if needed