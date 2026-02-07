# Repository Guidelines

## Project Structure & Module Organization

- `lib/`: **libgretl** core econometrics library (C).
- `cli/`: command-line programs (notably `gretlcli`).
- `gui/` + `editor/`: GTK-based GUI and editor support.
- `plugin/`: optional plugins (e.g., MPI/extra features, depending on configure flags).
- `addons/`: maintained function packages; built via `gretlcli` and (optionally) LaTeX docs.
- `share/`: runtime resources (functions, DTDs, desktop files).
- `tests/`: reference datasets and NIST regression checks.
- `unittests/`: Hansl (`.inp`) practice scripts and test scripts plus runners.
- `tools/`: build helpers and developer utilities.

## Build, Test, and Development Commands

Common workflow (from repo root):

```bash
./configure            # autodetect deps/features (see CONFIGURE-OPTIONS.md)
make                   # build lib/ cli/ gui/ plugins
make check             # run core test suite
make -C addons         # build addons (requires a built `cli/gretlcli`)
```

Notes:
- Addons docs require `pdflatex`; otherwise configure may disable addon building. Use `./configure --disable-build-addons` for headless builds.
- Troubleshooting: `BUILD-TROUBLESHOOTING.md`.
- Local LLM helper (optional): `./cli/gretlcli --ai='prompt' [--ai-provider=codex|gemini]`. Override binaries via `GRETL_CODEX_BIN` / `GRETL_GEMINI_BIN`.

## Coding Style & Naming Conventions

- C code: 4 spaces, K&R braces, keep functions small and error-checked.
- Prefer descriptive names (files and symbols), avoid gratuitous refactors.
- Autotools files: edit `configure.ac` / `Makefile.am` sources, then regenerate if you know the workflow; otherwise keep changes minimal.

## Testing Guidelines

- Core: `make check`.
- Script tests (Hansl): `./unittests/run_tests.sh --all`.
- New test scripts: name as `run_<feature>.inp` and keep them deterministic (no random inputs).

## Commit & Pull Request Guidelines

- Commit subjects are typically imperative with an optional scope prefix, e.g. `build: ...`, `gui: ...`, `lib: ...`.
- PRs should include: what changed, why, how to reproduce, and which commands were run (`make`, `make check`, relevant `unittests/` runner). Include platform/dependency notes when build behavior changes.

## Security & Configuration Tips

- Don’t introduce network calls in default test runs.
- Avoid writing outside the build tree; use `$(DESTDIR)` for install testing where applicable.
