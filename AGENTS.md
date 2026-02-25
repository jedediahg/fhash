# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Core C sources. `fhash.c` drives CLI argument handling and traversal; `utils.c` holds directory stack helpers; `hashing.c` wraps OpenSSL/FFmpeg hashing; `db.c` manages SQLite statements.  
- `include/`: Corresponding headers shared across modules.  
- `makefile`: Build targets for release, debug, install/uninstall, and cleanup.  
- `README.md`: User-facing usage and dependency notes.

## Build, Test, and Development Commands
- `make`: Release build with `-O3 -Wall`; produces `./fhash`.  
- `make debug`: Adds `-g -O0` for debugging symbols.  
- `make clean`: Removes binaries, objects, and generated DBs.  
- Run locally: `./fhash -s <path> -e mp3,flac -h -a -r -v -d ./file_hashes.db`.  
- Install (if needed): `sudo make install` / `sudo make uninstall`. Ensure SQLite3, OpenSSL, and FFmpeg dev libraries are present before building.

## Coding Style & Naming Conventions
- C code with 4-space indentation, no tabs; keep line length reasonable for readability.  
- Use `snake_case` for functions/variables, `UPPER_SNAKE_CASE` for macros/constants (e.g., `BATCH_SIZE`).  
- Prefer internal `static` helpers in `.c` files; expose only what is needed via headers in `include/`.  
- Handle errors with clear `fprintf(stderr, ...)` prefixes like existing messages; return non-zero on failure.  
- Build with `-Wall`; keep code warning-free. Keep portable `_GNU_SOURCE` needs in mind when adding system calls.

## Testing Guidelines
- No automated test suite yet. Perform manual runs against small sample trees before large scans.  
- Verify hashes and DB writes by querying: `sqlite3 file_hashes.db "SELECT COUNT(*) FROM files;"`.  
- When adding features, script a reproducible command line in your PR (start path, extensions, flags) and capture expected stdout/stderr.  
- If you add tests, mirror the build style in the makefile and place sources under `tests/` with clear fixture paths.

## Commit & Pull Request Guidelines
- Commit messages follow concise, present-tense summaries (see `git log`), e.g., “Fix format truncation warnings”.  
- Keep commits focused: one behavioral change per commit when possible.  
- PRs should include: goal/summary, key commands run (`make`, sample `./fhash ...` invocation), and notes on DB schema or dependency changes.  
- Link related issues when applicable; include screenshots/log excerpts only when they clarify behavior (verbose mode output is useful).

## Security & Configuration Tips
- Avoid trusting unvalidated file paths; prefer bounded copies (`snprintf`, `strncpy`) and check return codes as in existing code.  
- If adding new library calls, ensure matching cleanup (`sqlite3_finalize`, `avformat_close_input`, etc.) to prevent leaks.  
- Document any new configuration knobs in `README.md` and keep defaults safe (no destructive operations by default).
