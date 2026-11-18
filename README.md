# nevercode

A fast, unrestricted AI coding agent written in pure C.

No telemetry. No bloat. No restrictions. Full freedom.

## Features

- **58 built-in tools** — shell, file I/O, search, git, web fetch, multi-agent delegation, MCP, and more
- **Multi-provider** — OpenAI-compatible (Chat Completions) and Anthropic (Messages API) with automatic format adaptation
- **Long-running harness** — 4-level context compaction, goal tracking, stall detection, checkpoint discipline, context handoff
- **Multi-agent** — parallel sub-agents, coordinator mode, team management, inter-agent messaging
- **Session persistence** — full replay from JSONL logs, cross-session memory, resume with context continuity
- **Safe-by-default permission model** — workspace-scoped "standard" preset (approval required for network access and files outside the workspace); `-a full-access` for unrestricted mode
- **~88K lines of C**, <10ms startup, ~2.3MB binary, zero runtime dependencies beyond libcurl

## Build

```bash
make
make install          # optional — copies to /usr/local/bin
```

Requires: C11 compiler, `libcurl` (system). Optional: `readline` for line editing.

### Platform Support

| Platform | Status |
|----------|--------|
| macOS    | Fully supported (primary target). File watching via kqueue. |
| Linux    | Fully supported (build + full test suite verified on Debian 12). File watching via inotify. |
| Windows  | Not supported. Contributions welcome. |

## Quick Start

```bash
# OpenAI-compatible provider (default)
export OPENAI_API_KEY='your-key'
export OPENAI_BASE_URL='https://your-provider.example'
./nevercode

# Anthropic native provider
export ANTHROPIC_API_KEY='your-key'
./nevercode -P native

# One-shot prompt
./nevercode -p "explain quicksort in 3 sentences"

# Local Ollama
OPENAI_BASE_URL=http://127.0.0.1:11434/v1 OPENAI_API_KEY=dummy \
  ./nevercode -m llama3

# Resume previous session
./nevercode -c           # most recent
./nevercode -r <id>      # specific session
```

> **Security:** never commit API keys. Use environment variables or `-k` flag only.

## Configuration

Loading priority (later wins): **built-in defaults → config file → env vars → CLI flags**

### Config File

`~/.agents/config.json`:

```json
{
  "provider": "compat",
  "model": "gpt-5.4",
  "base_url": "https://your-provider.example",
  "max_tokens": 128000,
  "custom_instructions": "Always respond in English",
  "default_effort": "high"
}
```

API keys are **never** loaded from config files.

### CLI Options

| Flag | Description |
|------|-------------|
| `-P, --provider` | `compat` (default) or `native` |
| `-b, --base-url` | Custom API base URL |
| `-m, --model` | Model name |
| `-k, --key` | API key |
| `-p, --prompt` | One-shot prompt (non-interactive) |
| `-s, --system-prompt` | Custom system prompt |
| `-t, --max-tokens` | Max output tokens |
| `-e, --effort` | Reasoning effort: none/minimal/low/medium/high/xhigh/max |
| `-j, --json` | Structured JSON output |
| `-r, --resume` | Resume session by ID |
| `-c, --continue` | Resume most recent session |
| `-F, --profile` | Load config profile |
| `-a, --approval` | Approval preset: read-only / default / full-access |
| `-D, --dangerously-skip-permissions` | Skip all permission prompts |
| `-V, --version` | Show version |
| `-h, --help` | Show help |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | Compat provider API key |
| `OPENAI_BASE_URL` | Compat provider base URL |
| `ANTHROPIC_API_KEY` | Native provider API key |
| `ANTHROPIC_BASE_URL` | Custom native provider endpoint |

## Tools

58 built-in tools with full-access by default:

| Category | Tools |
|----------|-------|
| **File I/O** | Read, Write, Edit, MultiEdit, ApplyPatch, Glob, Grep, ListDir, FileSearch |
| **Execution** | Bash, REPL, Notebook |
| **Agent** | Agent, SpawnAgent, ListAgents, WaitAgent, CloseAgent, SendMessage, AgentControl |
| **Task** | Task, TodoWrite, Cron, TaskGet/List/Stop/Update/Output |
| **Team** | TeamCreate, TeamDelete, Consensus |
| **Git** | GitDiff |
| **Web** | WebFetch, WebSearch |
| **MCP** | MCP, McpAuth, ListMcpResources, ReadMcpResource |
| **Mode** | EnterPlanMode, ExitPlanMode, EnterExecuteMode, EnterPairMode, ResetMode |
| **Utility** | Config, Brief, Sleep, ImageView, Scaffold, Snip, ProcMonitor, ToolSearch, ToolSuggest, AskUser, RequestPerms, Batch, Workflow, StructuredOutput |
| **Worktree** | EnterWorktree, ExitWorktree |

## Built-in Skills

| Skill | Description |
|-------|-------------|
| **verify** | Adversarial testing of code changes |
| **debug** | Diagnostic-driven debugging |
| **simplify** | Code refactoring and simplification |
| **stuck** | Break out of loops with alternative approaches |
| **batch** | Apply transformations to multiple files |
| **security-review** | Security-focused code review |
| **perf** | Performance profiling and optimization |
| **test** | Generate tests for recent changes |
| **remember** | Save notes to AGENTS.md for future sessions |
| **loop** | Iterative execution until objective is met |
| **recon** | CTF reconnaissance and enumeration |
| **exploit** | Exploit development and testing |

## Long-Running Harness

nevercode is built around harness engineering — the model is one component; the rest is durable orchestration.

### Context Management

- **L1 Snip**: Zero-cost marker replacement for stale tool results
- **L2 MicroCompact**: Cache-aware bulk clearance when cache TTL expires
- **L3 Collapse**: Selective message folding without LLM calls
- **L4 AutoCompact**: Full LLM-based conversation summary with circuit breaker protection

### Autonomy Features

| Feature | Description |
|---------|-------------|
| Context Handoff | Structured state serialization when context approaches exhaustion |
| Goal Evolution | Stack-based goal tracker with automatic sub-goal splitting |
| Stall Detector | Sliding-window watchdog for loop/no-progress/timeout detection |
| Session Watchdog | Per-turn wall-clock timeout with HTTP streaming cutoff |
| Context Fatigue | EMA-based predictor for proactive compaction before quality degrades |
| Tool Verification | Post-execution scanner for silent tool failures |
| Output Evaluator | Heuristic scorer for goal alignment and completeness |
| Auto-Test | Self-verification loop matching edited sources to test files |
| Checkpoint Discipline | Automatic git checkpoints every N file-changing turns |
| Ghost Snapshots | Transparent per-turn git snapshots on detached refs |
| Resource Monitor | Local CPU/memory/disk monitoring (no data leaves the machine) |

### Failure Handling

- 5 error categories (transient, persistent, resource, semantic, fatal) with tailored retry strategies
- Adaptive rate control with sliding-window pressure tracking (up to 8x backoff)
- Circuit breaker on compaction (3 consecutive failures → stop)

## Multi-Agent

- **Coordinator Mode**: goal decomposition → task delegation → progress monitoring → result merge
- **Agent Roles**: default, explorer, awaiter, reviewer, recon, exploit, verifier (custom roles via JSON)
- **Swarm Backends**: in-process, tmux, or external process (auto-detected with fallback)
- **Mailbox**: inter-agent messaging with sequence tracking and FIFO delivery
- **Team Memory Sync**: shared key-value store with built-in secret scanning

## Session System

- Full session replay from JSONL event logs
- Cross-session memory with provenance citations
- Rollout truncation for bounded disk usage
- Session modes (default, coordinator, plan, execute, pair) persisted per session
- Auto session titles from local heuristics (no API call)
- Background memory consolidation during idle periods

## Plugin System

Shared-object plugins (`.so`/`.dylib`) in `~/.agents/plugins/` with lifecycle hooks:

```c
void  *plugin_init(void);
void   plugin_unload(void *ctx);
int    plugin_hook(int event, const char *data, char **response, void *ctx);
const char *plugin_name(void);
const char *plugin_version(void);
```

## Architecture

```
nevercode/
├── include/          # 15 headers (~1300 macros in defaults.h)
├── src/              # 355 source files across 13 modules
│   ├── core/         # Main loop, config, conversation, state machine
│   ├── api/          # Provider implementations, streaming, auth
│   ├── tools/        # 58 built-in tools
│   ├── context/      # Context management, 4-level compaction
│   ├── agents/       # Multi-agent orchestration
│   ├── memory/       # Session history, memory pipeline
│   ├── exec/         # Tool execution, process management
│   ├── harness/      # Long-running orchestration
│   ├── ui/           # Terminal, rendering, vim mode
│   ├── utils/        # Fuzzy matching, encoding, caching
│   ├── review/       # Code review, risk assessment
│   ├── system/       # Permissions, sandbox, model registry
│   └── state/        # Session persistence, rollout tracking
├── tests/            # 416 test files (414 unit suites + 1 integration)
├── scripts/          # Development utilities
└── third_party/      # cJSON (embedded)
```

### Stats

| Metric | Value |
|--------|-------|
| Source | ~88K lines of C (15 headers + 356 sources) |
| Tests | 414 unit suites + 1 integration (all passing) |
| Binary | ~2.3MB (~2.1MB stripped) |
| Startup | <10ms |
| Memory | <5MB |
| Dependencies | libcurl (system), cJSON (embedded) |
| Telemetry | None |

## Security

- **Permissions** — nevercode runs with the `standard` approval preset by default: file operations are allowed inside the workspace (and `$TMPDIR`), while **network access (WebFetch/WebSearch) and files outside the workspace require approval** (or `--dangerously-skip-permissions`/`-D` for unrestricted auto-allow, or `-a full-access`). `-a read-only` restricts to read-only tools.
- **Network egress is restricted to http(s)** — WebFetch/WebSearch follow redirects only within http/https and refuse connections to loopback, private, link-local, CGNAT, multicast and reserved addresses (including `169.254.169.254` cloud metadata) at connect time.
- **Project-local plugins are not loaded by default** — `.agents/plugins/` and `.claude/plugins/` inside a repo are ignored unless you opt in with `NEVERCODE_PROJECT_PLUGINS=1`. User-level plugins (`~/.agents/plugins/`, `~/.claude/plugins/`) load from regular, owner-writable-only files; signatures are not yet verified.
- **Telemetry: none** — no analytics, crash reporting, or usage tracking of any kind.
- **API keys** are held in memory only; they are never written to disk by the tool itself.

## Testing

```bash
make test              # 414 unit test suites
make test-all          # Unit + integration
make test-harness      # Build + test + binary validation
make test-harness-json # With JSON report output
```

## Project Instructions

nevercode loads project instructions from `AGENTS.md` (and legacy `CLAUDE.md`) with override/local/base precedence. Searches project tree upward and user home (`~/.agents/`).

## License

See [LICENSE](LICENSE).
