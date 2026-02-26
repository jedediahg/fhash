# fhash Code Review

Date: 2026-02-26  
Scope: `src/*.c`, `include/*.h`, `tests/run_tests.sh`, `makefile`, `README.md`

## Executive Summary

`fhash` is generally clean and modular, with clear separation between scan/hash/db/duplicate workflows. The most important issue is a correctness regression in incremental scanning: existing rows are never revalidated unless `-f` is passed, which can leave stale hashes indefinitely. On performance, current scanning does unnecessary per-file DB round-trips and file-level memory mappings that will limit throughput on large libraries.

## Findings (ordered by severity)

### 1) Incremental scan behavior is incorrect (stale hashes are never refreshed)
- Severity: High
- Location: `src/fhash.c:194-203`, `src/fhash.c:209-215`, `README.md:14`
- Problem:
  - Existing files are skipped unless `-f` is set.
  - There is no mtime/size-based change detection before skipping.
  - This conflicts with documented behavior: "Only updates changed files unless forced."
- Impact:
  - If file content changes at the same path, DB hash values become stale and duplicate/link output can be wrong.
- Suggested fix:
  - Store and compare `st_mtime`/`st_ctime` (or content fingerprint metadata) and `filesize`.
  - Process updates when these values differ, even without `-f`.
  - Add a test case in `tests/run_tests.sh`: scan file, mutate file, rescan without `-f`, assert hash changed.

### 2) Scan path does one SELECT per file before insert/update
- Severity: High (performance)
- Location: `src/fhash.c:135-143`, `src/fhash.c:195-207`
- Problem:
  - Every candidate file performs a lookup query first, then either insert/update.
- Impact:
  - Heavy DB syscall and statement-step overhead on large trees.
- Suggested fix:
  - Replace pre-check pattern with a single UPSERT statement:
    - `INSERT ... ON CONFLICT(filepath) DO UPDATE ...`
  - Add `WHERE` clause in UPSERT update branch to skip unchanged rows (mtime/filesize check).

### 3) Full-file `mmap()` hashing is risky for very large files
- Severity: Medium (performance/stability)
- Location: `src/hashing.c:144-171`
- Problem:
  - `calculate_md5()` maps entire file into memory.
- Impact:
  - Large files can create address-space pressure, major page-fault churn, or mapping failures.
- Suggested fix:
  - Switch to streaming hash (`read` in fixed-size chunks, e.g., 1-8 MiB) with `EVP_DigestUpdate`.
  - This is usually more predictable under memory pressure and scales better in mixed workloads.

### 4) Duplicate/link DB updates are not wrapped in an explicit transaction
- Severity: Medium (performance)
- Location: `src/utils.c:300-314`, `src/utils.c:260-288`
- Problem:
  - Each update during link mode commits individually under autocommit.
- Impact:
  - Significant slowdown for many links due to per-row commit overhead.
- Suggested fix:
  - Begin one transaction for `process_duplicates()` when in link mode and commit once at end.
  - Roll back on fatal errors.

### 5) No indexes on duplicate query columns
- Severity: Medium (performance)
- Location: `src/db.c:79-83`, `src/utils.c:316-322`
- Problem:
  - Duplicate query orders/groups logically by `md5`/`audio_md5`, but schema only has unique index on `filepath`.
- Impact:
  - Full table scans and sorting for large datasets.
- Suggested fix:
  - Add indexes:
    - `CREATE INDEX IF NOT EXISTS idx_files_md5 ON files(md5);`
    - `CREATE INDEX IF NOT EXISTS idx_files_audio_md5 ON files(audio_md5);`
    - Optionally `idx_files_extension`, `idx_files_last_check_timestamp` based on query patterns.

### 6) Extension filtering has avoidable O(N*M) cost
- Severity: Medium (performance)
- Location: `src/fhash.c:185-191`
- Problem:
  - For every file, extension is linearly compared with every provided extension.
- Impact:
  - Noticeable overhead with many files and larger extension lists.
- Suggested fix:
  - Normalize extensions once and use a hash-set-like structure (or sorted array + binary search).
  - Also normalize to lowercase once at parse time.

### 7) Potential NULL dereference on `strdup()` result
- Severity: Medium
- Location: `src/fhash.c:120-122`, `src/fhash.c:402-404`
- Problem:
  - `strdup()` return values are not checked before passing into `strtok()`.
- Impact:
  - On allocation failure, can crash.
- Suggested fix:
  - Check `ext_copy` for `NULL` and fail gracefully.

### 8) Path truncation risk in directory stack
- Severity: Medium
- Location: `src/utils.c:58-60`, `src/fhash.c:146-149`
- Problem:
  - Paths are copied into fixed `MAX_PATH_LENGTH` buffers with truncation.
- Impact:
  - Truncated paths can cause wrong traversal, stat/open failures, or unexpected behavior.
- Suggested fix:
  - Detect truncation (`strlen(path) >= MAX_PATH_LENGTH`) and skip with explicit error.
  - Better: store dynamically allocated path strings in `DirStack`.

### 9) Transaction error paths during batch commits are not handled robustly
- Severity: Medium
- Location: `src/fhash.c:217-220`
- Problem:
  - Return values from `commit_transaction()` and `begin_transaction()` inside the loop are ignored.
- Impact:
  - Scan may continue in inconsistent DB state after commit failure.
- Suggested fix:
  - Check both calls, abort scan on failure, and report the failing file/batch context.

### 10) SQL style/readability issues (`IS NOT` used for string comparisons)
- Severity: Low (sloppy)
- Location: `src/utils.c:319-321`
- Problem:
  - `IS NOT` is used with string literals where `!=` is more conventional for this use.
- Impact:
  - Harder to read/maintain; subtle semantics are less obvious.
- Suggested fix:
  - Prefer `column NOT IN ('N/A', 'Not calculated', 'Bad audio', '0-byte-file')`.

### 11) Minor CLI/message quality issues
- Severity: Low (sloppy)
- Location: `src/fhash.c:340`
- Problem:
  - Typo in error string: `unkown`.
- Impact:
  - Cosmetic.
- Suggested fix:
  - Correct message text and include offending arg for easier debugging.

## Efficiency Improvement Plan (scan/hash focused)

1. Add change detection columns and logic (`mtime`, maybe inode/dev) to avoid stale rows and avoid unnecessary rehashing.
2. Collapse read-before-write flow into UPSERT + conditional update.
3. Replace mmap-based full-file hashing with chunked streaming.
4. Add hash indexes (`md5`, `audio_md5`) and measure with `EXPLAIN QUERY PLAN`.
5. Wrap link-mode update operations in one transaction.
6. Optimize extension matching data structure.
7. Consider parallel hashing worker pool (bounded queue) once single-thread bottlenecks are addressed.

## Testing Gaps

- No test verifies incremental update correctness (changed file without `-f`).
- No performance regression tests (even simple timing thresholds) for large synthetic trees.
- No test for extremely long paths (stack truncation behavior).
- No tests for low-memory/error-path behavior around extension parsing.

## What I verified locally

- Built with `make` and ran `./tests/run_tests.sh` successfully.
- Existing smoke tests pass; they do not currently catch the stale-row incremental scan issue.
