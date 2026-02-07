# AI Assistant (local Codex/Gemini)

This repository includes an **experimental** AI assistant integrated into:

- `gretl_x11` (GUI): **Tools → AI Assistant…**
- `gretlcli` (CLI): one-shot helper flag `--ai=...`

The assistant is **safe-by-default**: it does not auto-run generated code. The GUI can copy the reply or (with confirmation) insert text into the active script editor.

## Requirements

Gretl does not ship an LLM. You must install at least one local CLI:

- Codex CLI (`codex`)
- Gemini CLI (`gemini`)

Gretl spawns these binaries; any authentication/credentials are managed by the CLI itself (not by gretl).

## Usage (GUI)

Open **Tools → AI Assistant…** and:

- Choose provider: `codex` or `gemini`
- Optionally include context:
  - Dataset summary
  - Last error/warning
  - Script selection (or whole buffer if no selection)
- Optional: **Enable tools (read-only)** — allows a 2-step “tool loop” where the model can request specific read-only context and then answer using it.

### Read-only tools available

When tools are enabled, the model may request:

- `get_dataset_summary`
- `get_last_error`
- `get_script_selection`
- `get_script_full`
- `get_command_log_tail` (argument: `n_lines`)
- `get_last_model_summary` (argument: `style` = `simple|full`)

Tool execution is implemented inside gretl (no network; no file writes). Results are appended to the prompt and a second model call produces the final answer.

## Usage (CLI)

One-shot helper (prints text and exits):

```bash
./cli/gretlcli --ai='Explain this error: ...'
./cli/gretlcli --ai='Write a hansl script to ...' --ai-provider=codex
./cli/gretlcli --ai='Summarize this output ...' --ai-provider=gemini
```

## Configuration (environment variables)

Provider selection:

- `GRETL_LLM_PROVIDER=codex|gemini` (default: `codex`)

Binary overrides:

- `GRETL_CODEX_BIN=/path/to/codex`
- `GRETL_GEMINI_BIN=/path/to/gemini`

Timeout (Linux): gretl wraps the spawned process with `timeout` if available:

- `GRETL_LLM_TIMEOUT_SEC` (default 300; range 1–3600)

Unsafe mode (Codex only):

- `GRETL_LLM_UNSAFE=1` (or `GRETL_CODEX_DANGEROUS=1`) makes gretl call:
  `codex exec --dangerously-bypass-approvals-and-sandbox ...`

This can allow the provider to execute shell commands and write files without prompts. It is not recommended as a default for general users; prefer keeping the assistant in read-only mode and using the built-in read-only tools.

## Approvals, sandbox, and MCP servers

### Codex approvals/sandbox

By default, gretl runs Codex in a restricted mode (no approvals, read-only sandbox). This is intentional: it prevents “agentic” shell execution from inside the gretl GUI.

If you explicitly opt in via `GRETL_LLM_UNSAFE=1`, gretl will invoke Codex with `--dangerously-bypass-approvals-and-sandbox`. Use this only if you understand the risks and you trust the prompts/context being sent.

### MCP servers

Gretl does not manage MCP servers. If you want Codex to use MCP tools, configure them in Codex itself (typically via `~/.codex/config.toml` or the `codex mcp` subcommands):

```bash
codex mcp list
codex mcp add --help
```

Note: Gemini is invoked with MCP/tools disabled by default to avoid hangs in non-interactive mode.

## Implementation overview

Key modules:

- `lib/src/gretl_llm.c`, `lib/src/gretl_llm.h`
  - Provider-neutral API: `gretl_llm_complete[_with_error]()`
  - Spawns `codex`/`gemini` via GLib (`g_spawn_sync`)
  - Gemini output is parsed from JSON; MCP/tools are disabled to avoid hangs
- `gui/ai_assistant.c`, `gui/ai_assistant.h`
  - Tools menu entry opens a viewer window
  - Runs LLM call(s) in a background `GThread`, updates GTK via `g_idle_add`
  - Builds prompt from user text + selected context
  - Parses strict JSON replies (best-effort) when tools are enabled
- `gui/dialogs.c`, `gui/dialogs.h`
  - Stores “last error/warning” strings for assistant context
- `cli/gretlcli.c`
  - Adds `--ai=` and `--ai-provider=` for one-shot use

## Notes / limitations

- The read-only tool loop is intentionally minimal: it’s a small JSON protocol, not a general-purpose agent framework.
- “Last model summary” currently supports the last equation model (`GRETL_OBJ_EQN`); other model object types return a short “not supported” message.
