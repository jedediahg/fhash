#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${ROOT}/test_results.txt"
WORK="${ROOT}/tests/workdir"
SRC="${ROOT}/test_source"
DB="${WORK}/test.db"

echo "[INFO] Preparing test workspace..."
rm -f "${OUT}"
rm -rf "${WORK}"
mkdir -p "${WORK}/dupes"

log_start() {
    echo "=== START: $1 ===" >> "${OUT}"
}

log_result() {
    local status=$1
    local msg=$2
    if [ "$status" -eq 0 ]; then
        echo "=== SUCCESS: ${msg} ===" >> "${OUT}"
    else
        echo "=== FAILED: ${msg} (exit ${status}) ===" >> "${OUT}"
    fi
    echo >> "${OUT}"
}

run_step() {
    local label="$1"; shift
    log_start "$label"
    set +e
    "$@" >> "${OUT}" 2>&1
    local rc=$?
    set -e
    log_result "$rc" "$label"
    return "$rc"
}

echo "[INFO] Copying source samples (root files only, ignoring other_songs)..."
find "${SRC}" -maxdepth 1 -type f -name "*.mp3" -print0 | while IFS= read -r -d '' f; do
  cp "$f" "${WORK}/"
done

echo "[INFO] Seeding dupes folder from copied samples..."
find "${WORK}" -maxdepth 1 -type f -name "*.mp3" -print0 | while IFS= read -r -d '' f; do
  cp "$f" "${WORK}/dupes/"
done

# 1) Scan with both hashes to populate DB
run_step "scan (file+audio over workdir)" "${ROOT}/fhash" scan -v -r -h -a -f -s "${WORK}" -e mp3 -d "${DB}"
run_step "scan results (md5/audio_md5 summary)" sqlite3 "${DB}" "SELECT filename, md5, audio_md5 FROM files ORDER BY filename;"

# 2) File-hash duplicates (should group identical hard-link hearts copies)
run_step "dupe by file hash" "${ROOT}/fhash" dupe -v -xh2 -s "${WORK}" -r -e mp3 -d "${DB}"

# 3) Audio-hash duplicates (should group take 2 variants with different metadata)
run_step "dupe by audio hash" "${ROOT}/fhash" dupe -v -xa2 -s "${WORK}" -r -e mp3 -d "${DB}"

# 4) Link dry-run on dupes folder (file-hash mode) to show planned hardlinks
run_step "link dry-run (file hash, shallowest)" "${ROOT}/fhash" link -v -xh2 -ls -s "${WORK}/dupes" -r -e mp3 -d "${DB}" -dry

# 5) Sentinel coverage check for 0-byte and bad-audio entries
run_step "sentinel rows check" sqlite3 "${DB}" "SELECT filename, md5, audio_md5 FROM files WHERE md5='0-byte-file' OR audio_md5='Bad audio' ORDER BY filename;"

echo "[INFO] Results written to ${OUT}"
