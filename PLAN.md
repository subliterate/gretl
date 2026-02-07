# PLAN: Integrating Codex + Gemini into Gretl (API + GUI)

## 1) Summary

Add an “AI Assistant” capability to Gretl that can:

- Generate and edit **hansl** scripts and Gretl command sequences.
- Explain model output, errors, and data issues in plain language.
- Offer “one-click” actions (insert code, open a script, run a preview), while keeping the user in control.

Implementation is split into:

1. A **provider-agnostic LLM layer** in `libgretl` (HTTP + JSON).
2. A **GUI integration** (GTK panel + editor/console hooks) in `gretl_x11`.
3. Optional **CLI integration** in `gretlcli` / `gretl_sh`.

Codex and Gemini are treated as *providers* behind a common interface.

---

## 2) Goals / Non-goals

### Goals

- Provider abstraction: swap between **Codex/OpenAI** and **Gemini** at runtime.
- Safe-by-default UX: no silent execution of AI-generated code.
- Context-aware assistance: optionally include dataset/model/script context.
- Reproducibility: allow saving prompts + responses alongside scripts/sessions.
- Minimal new dependencies: prefer existing stack (`libcurl`, `json-glib`) where feasible.

### Non-goals (initially)

- Fully autonomous agents that execute system commands.
- Uploading full datasets by default.
- “Always-on” telemetry or cloud logging.
- Replacing existing help/docs; this is an assistant, not a requirement.

---

## 3) User-facing features (MVP)

### GUI (gretl_x11)

- **Dockable “AI Assistant” panel** with:
  - Chat transcript
  - Provider selector: Codex | Gemini
  - Context toggles: Active dataset | Current model | Current script | Last error
  - Buttons: `Insert Script`, `Explain Output`, `Fix Error`, `Summarize Dataset`
- **Diff / preview** before applying any generated script changes.
- A “Copy prompt bundle” button to export the exact context used (for debugging and reproducibility).

### CLI (gretlcli / gretl_sh)

- Optional flag to call the assistant for:
  - “Explain this output/error”
  - “Generate a hansl script from this description”
- Output is printed; any executable content is clearly labeled as a suggestion.

---

## 4) Architecture overview

### High-level dataflow

```
GUI/CLI
  |
  | (user prompt + selected context)
  v
Context Builder  ---> Redaction/Token Budget ---> Prompt Bundle (JSON)
  |
  v
Provider Layer (Codex | Gemini)
  |
  v
Response Parser (text + optional tool calls)
  |
  v
UI Actions (insert / preview / run) + Persist (optional)
```

### Key design principle

All model calls go through a single library-level API so:

- GUI and CLI share logic.
- Provider changes don’t affect UI code.
- We can unit-test behavior using mock HTTP responses.

---

## 5) Provider abstraction

Create a small provider interface (C) with implementations:

- `provider_openai.c` (Codex/OpenAI-style Chat Completions or Responses API)
- `provider_gemini.c` (Gemini REST API)

Common responsibilities:

- Build HTTP request JSON from a provider-neutral “prompt bundle”.
- Attach safety settings (where supported).
- Parse model response into:
  - `assistant_text`
  - `suggested_edits` (optional, e.g., full hansl script)
  - `tool_calls` (optional; see §6)
- Normalize errors (HTTP error, rate limit, auth, invalid JSON) into Gretl-friendly messages.

Notes:

- Prefer `libcurl` + `json-glib` (already used in Gretl builds) for portability.
- Providers should be selectable via config and overridable via env vars for CI/testing.

---

## 6) “Tools” / function-calling (optional but recommended)

To make responses actionable and safer than free-form code, define a constrained tool set
that the UI can execute only with explicit confirmation.

### Proposed tool schema (provider-neutral)

- `get_session_state`:
  - Returns dataset metadata (nobs, variables, labels), current sample, and open script name.
- `get_model_summary`:
  - Returns a compact text/JSON summary of the last model (coefficients, fit stats).
- `get_last_error`:
  - Returns last error message + command history tail.
- `propose_hansl_script`:
  - Returns a complete hansl script as a string (never executed automatically).
- `propose_patch`:
  - Returns a unified diff patch against the current script buffer.

### Execution rules

- Only allow read-only tools without prompts.
- Any “write” or “run” action requires:
  - Preview (diff)
  - Explicit user confirmation
  - Clear provenance (which provider/model produced it)

If a provider does not support native tool-calling, simulate it by:

1. Instructing the model to output a strict JSON object.
2. Parsing/validating it locally, with a fallback to plain text.

---

## 7) Context builder (what the model sees)

Context should be explicit, minimal, and user-controlled.

### Context sources (toggleable)

- Active dataset summary:
  - variable names, labels, types, missingness, basic stats (bounded size)
- Current model summary:
  - model type + key fit statistics + coefficient table (truncated)
- Current editor buffer:
  - selected text or full script (user choice)
- Last error + recent command history

### Token/size budgeting

- Set a hard byte limit per context segment and overall request payload.
- Truncate with clear markers (e.g., `...[truncated]...`).
- Never attach raw dataset by default; require explicit export + selection.

### Redaction

- Provide a redaction pass for:
  - file paths (optional)
  - user names/hostnames (optional)
  - any text flagged as “private” by the UI selection model

---

## 8) Integration points in Gretl

### libgretl

- Add a small “LLM client” module that exposes:
  - `gretl_llm_request(prompt_bundle, provider, options, &response)`
  - `gretl_llm_last_error()` for UI display
- Keep it independent of GTK; pure C + existing networking/JSON deps.

### GUI (gretl_x11)

- New dockable panel:
  - conversation view (GtkTextView)
  - prompt input (GtkTextView or GtkEntry + multi-line toggle)
  - context toggles and provider selector
  - “Apply” actions with diff preview
- Hook into:
  - script editor buffer (to read selection / apply patches)
  - model window (to pull summaries)
  - console/error reporting (to grab last error)

### CLI

- Add commands/flags:
  - `--ai "prompt..."` (no context)
  - `--ai-with=dataset,model,script,error` (explicit contexts)
- Save prompt bundles and responses to a user-selected file when requested.

---

## 9) Configuration, secrets, and privacy

### Configuration sources (priority order)

1. CLI flags (one-shot)
2. Environment variables
3. Gretl config file

### Keys

- Use env vars by default:
  - `OPENAI_API_KEY` (Codex/OpenAI)
  - `GEMINI_API_KEY` (Gemini)
- Optional: integrate with OS keyring later (Linux libsecret, macOS Keychain, Windows Credential Manager).

### Privacy defaults

- Off by default unless enabled in preferences (recommended for first rollout).
- Clear indication in the UI when context is attached.
- A “Send minimal context” mode that never attaches script/dataset/model automatically.

---

## 10) Build + packaging strategy

- Introduce `./configure` options:
  - `--enable-llm` (default: off for conservative packagers; on for developer builds if desired)
  - `--with-openai` / `--with-gemini` (or runtime-only if both use the same HTTP stack)
- Keep provider code behind feature flags so downstreams can build without cloud features.
- Document all new deps and ensure they are optional where possible.

---

## 11) Testing strategy

### Unit tests (preferred)

- Context builder:
  - truncation behavior
  - redaction behavior
  - deterministic output given fixed inputs
- Provider request construction:
  - valid JSON shape
  - correct headers
- Response parsing:
  - plain text
  - structured tool-call JSON
  - malformed responses

### Integration tests

- Start a local mock HTTP server (or use a simple fixture) to return canned provider responses.
- Verify GUI actions are gated behind confirmation and produce correct diffs/insertions.

---

## 12) Phased implementation plan

### Phase 0 — Discovery + design (1–2 weeks)

- Identify best existing hooks for:
  - editor buffer access
  - extracting model summaries
  - “last error” retrieval
- Draft provider-neutral prompt bundle schema and tool schema.

### Phase 1 — libgretl LLM core (1–2 weeks)

- Implement context builder + redaction + token/size budgeting.
- Implement provider abstraction and one provider end-to-end (start with the simpler API).
- Add unit tests with mock responses.

### Phase 2 — GUI MVP (2–4 weeks)

- Add dockable AI panel with chat + context toggles.
- Add “Insert Script” and “Explain Output” flows.
- Add diff preview + apply confirmation for any edits.

### Phase 3 — Tools/function-calling + richer actions (2–4 weeks)

- Add tool schema support and safe execution gating.
- Add “Fix Error” with patch proposal against current script.
- Add “Generate function package skeleton” (optional).

### Phase 4 — CLI support + docs + packaging (1–2 weeks)

- Add CLI flags and response persistence.
- Document config, privacy, and examples.
- Add packager notes for optional build flags.

---

## 13) Open questions

- Provider API choice for “Codex”:
  - use OpenAI Responses API directly, or call an installed `codex` CLI as an external helper?
- What is the minimal, stable “model summary” representation across model types?
- How should prompts and responses be stored for reproducibility (project file vs. sidecar files)?
- Should the assistant be enabled by default in official builds, or opt-in only?

