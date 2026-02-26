# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Core C. `fhash.c` parses commands (`scan|dupe|link`), `utils.c` handles duplicate grouping/linking and path filtering, `hashing.c` wraps OpenSSL/FFmpeg hashing, `db.c` owns schema/version checks and transactions.  
- `include/`: Headers for the above modules.  
- `tests/`: `run_tests.sh` plus `README.md`; uses fixtures in `test_source/` to exercise scan/dupe/link.  
- `test_source/`: Generated MP3 fixtures (identical, metadata-variant, 0-byte, bad-audio). `other_songs/` is ignored.  
- `makefile`: Build/install/clean targets.  
- `README.md`: Usage, DB schema, option descriptions.

## Build, Test, and Development Commands
- `make` / `make debug` / `make clean` — build or clean; binary is `./fhash`.  
- Quick scan example: `./fhash scan -s <path> -e mp3,flac -h -a -r -v -d ./file_hashes.db`.  
- Duplicate listing: `./fhash dupe -xh2 -s <path> -r -e mp3`.  
- Link dry-run: `./fhash link -xa2 -ls -s <path> -r -e mp3 -dry`.  
- Tests: `./tests/run_tests.sh` (writes `test_results.txt`, recreates `tests/workdir/` each run).

## Coding Style & Naming Conventions
- C code with 4-space indentation, no tabs; keep line length reasonable for readability.  
- Use `snake_case` for functions/variables, `UPPER_SNAKE_CASE` for macros/constants (e.g., `BATCH_SIZE`).  
- Prefer internal `static` helpers in `.c` files; expose only what is needed via headers in `include/`.  
- Handle errors with clear `fprintf(stderr, ...)` prefixes like existing messages; return non-zero on failure.  
- Build with `-Wall`; keep code warning-free. Keep portable `_GNU_SOURCE` needs in mind when adding system calls.

## Testing Guidelines
- `tests/run_tests.sh` is the canonical smoke test; it scans fixtures, lists dupes by file/audio hash, runs link dry-run, and checks sentinel rows.  
- Fixtures are regenerated into `tests/workdir/` each run; `test_results.txt` captures START/SUCCESS/FAILED markers.  
- For new features, add focused steps to the script or mirror its pattern; keep outputs minimal and labeled.

## Commit & Pull Request Guidelines
- Commit messages follow concise, present-tense summaries (see `git log`), e.g., “Fix format truncation warnings”.  
- Keep commits focused: one behavioral change per commit when possible.  
- PRs should include: goal/summary, key commands run (`make`, sample `./fhash ...` invocation), and notes on DB schema or dependency changes.  
- Link related issues when applicable; include screenshots/log excerpts only when they clarify behavior (verbose mode output is useful).

## Security & Configuration Tips
- Avoid trusting unvalidated file paths; prefer bounded copies (`snprintf`, `strncpy`) and check return codes as in existing code.  
- If adding new library calls, ensure matching cleanup (`sqlite3_finalize`, `avformat_close_input`, etc.) to prevent leaks.  
- Document any new configuration knobs in `README.md` and keep defaults safe (no destructive operations by default).
