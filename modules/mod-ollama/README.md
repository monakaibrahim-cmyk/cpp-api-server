# mod_ollama - Local Ollama Model Integration, Agent Auto-Discovery & Session Caching

`mod_ollama` integrates local Ollama large language model (LLM) instances with the API framework. It provides automated agent discovery, multi-turn chat generation, and IP-identified conversation sessions with in-memory LRU cache persistence.

## Key Features

- **Local Ollama Daemon Communication**:
  - Connects to a locally running Ollama instance (default: `http://127.0.0.1:11434`).
  - High-performance asynchronous HTTP 1.1 client using Boost.Beast.
  - Configurable timeouts and connection health checks.

- **Automated Agent Discovery**:
  - Automatically queries Ollama's `/api/tags` and `/api/ps` to discover all locally installed models.
  - Treats discovered models as available "agents" (e.g. `llama3`, `mistral`, `deepseek-r1`, `phi3`).
  - Exposes agent metadata: parameter size, quantization level, file format, running state.
  - Auto-selects the active default agent when none is specified.
  - Caches discovered agents to minimize network overhead with optional `?refresh=true` bypass.

- **IP-Identified Session Management via Cache**:
  - Users are identified by their client IP address (`X-Forwarded-For`, `X-Real-IP`, or TCP socket address).
  - Sessions are managed through the framework's high-performance in-memory LRU `cache_engine`.
  - Tracks session creation, last active time, selected agent, and total message counts.
  - Configurable session TTL (default: 3600 seconds), refreshed automatically upon activity.

- **Conversation History Caching**:
  - Multi-turn conversation turns (`user`, `assistant`, `system`) are stored in the LRU cache per client IP.
  - Chat completions automatically include prior conversation context for coherent multi-turn chats.
  - Dedicated endpoints to inspect (`GET /api/chat/history`) or clear (`DELETE /api/chat/history`) chat history.

## API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/ollama/status` | Connection status, version, and cache statistics |
| `GET` | `/api/agents` | Auto-discovers and lists all installed Ollama agents |
| `GET` | `/api/ollama/agents` | Alias for `/api/agents` |
| `POST` | `/api/chat` | Send a chat message with IP-based history & session |
| `POST` | `/api/ollama/chat` | Alias for `/api/chat` |
| `GET` | `/api/chat/history` | Retrieve cached chat history for caller's IP |
| `DELETE` | `/api/chat/history` | Clear cached chat history and session for caller's IP |
| `POST` | `/api/chat/clear` | Alternative POST endpoint to clear chat history |
| `GET` | `/api/session` | Inspect current session metadata for caller's IP |
| `DELETE` | `/api/session` | Invalidate active session for caller's IP |

## Chat Request Format

```json
POST /api/chat
Content-Type: application/json

{
  "message": "Hello, how are you?",
  "agent": "llama3:latest",
  "system": "You are a helpful coding assistant.",
  "clear_history": false
}
```

- `message` (required): The prompt from the user.
- `agent` (optional): Model/agent name. If omitted, uses session's current agent or auto-discovered default.
- `system` (optional): Custom system instruction overriding the default.
- `clear_history` (optional): If `true`, resets history before sending this prompt.
