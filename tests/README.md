# Tests

`run_tests.sh` exercises the core workflows against generated sample MP3s in `test_source/`:

- Scans the workspace copy with file+audio hashes and summarizes DB rows.
- Finds duplicate groups by file hash and by audio hash (covers identical files and metadata-different copies).
- Performs a link dry-run on the dupes folder (shows intended hardlinks without modifying files).
- Confirms sentinel handling for `0-byte-file` and `Bad audio` cases.

The script writes annotated results to `test_results.txt` with clear START/SUCCESS/FAILED markers. Fixtures are copied into `tests/workdir/`, so reruns start from a clean slate.
