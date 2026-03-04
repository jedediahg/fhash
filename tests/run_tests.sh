#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${ROOT}/test_results.txt"
WORK="${ROOT}/tests/workdir"
SRC="${ROOT}/test_source"
DB="${WORK}/test.db"
MIG_DB="${WORK}/legacy_v1_0.db"

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

# 2b) Audio stream validation and enum persistence
run_step "check audio streams" "${ROOT}/fhash" check -v -r -s "${WORK}" -e mp3 -d "${DB}"
run_step "check results (audio_check_result summary)" sqlite3 "${DB}" "SELECT filename, audio_check_result FROM files ORDER BY filename;"
run_step "check sentinel values (0-byte=1, all checked)" bash -lc "sqlite3 '${DB}' \"SELECT COUNT(*) FROM files WHERE extension='mp3' AND audio_check_result=4;\" | grep -qx '0' && sqlite3 '${DB}' \"SELECT audio_check_result FROM files WHERE filename='0bytes.mp3';\" | grep -qx '1'"
run_step "check force re-run with -f" bash -lc "'${ROOT}/fhash' check -v -f -r -s '${WORK}' -e mp3 -d '${DB}' 2>&1 | tee '${WORK}/check_force.log' && grep -q 'Treated 12 files\\.' '${WORK}/check_force.log'"

# 3) Audio-hash duplicates (should group take 2 variants with different metadata)
run_step "dupe by audio hash" "${ROOT}/fhash" dupe -v -xa2 -s "${WORK}" -r -e mp3 -d "${DB}"

# 4) Link dry-run on dupes folder (file-hash mode) to show planned hardlinks
run_step "link dry-run (file hash, shallowest)" "${ROOT}/fhash" link -v -xh2 -ls -s "${WORK}" -r -e mp3 -d "${DB}" -dry

# 5) Sentinel coverage check for 0-byte and bad-audio entries
run_step "sentinel rows check" sqlite3 "${DB}" "SELECT filename, md5, audio_md5 FROM files WHERE md5='0-byte-file' OR audio_md5='Bad audio' ORDER BY filename;"

# 6) Incremental update without -f (mtime/filesize change should trigger rehash)
run_step "incremental baseline md5" bash -lc "sqlite3 '${DB}' \"SELECT md5 FROM files WHERE filepath='${WORK}/Hard Link Hearts.mp3';\" > '${WORK}/md5_before.txt' && test -s '${WORK}/md5_before.txt'"
run_step "mutate tracked file" bash -lc "printf 'x' >> '${WORK}/Hard Link Hearts.mp3'"
run_step "incremental rescan without -f" "${ROOT}/fhash" scan -v -r -h -s "${WORK}" -e mp3 -d "${DB}"
run_step "incremental md5 changed check" bash -lc "sqlite3 '${DB}' \"SELECT md5 FROM files WHERE filepath='${WORK}/Hard Link Hearts.mp3';\" > '${WORK}/md5_after.txt' && test -s '${WORK}/md5_after.txt' && ! cmp -s '${WORK}/md5_before.txt' '${WORK}/md5_after.txt'"

# 7) Migration coverage: upgrade legacy 1.0 DB to 1.01 and backfill check results
run_step "create legacy 1.0 DB fixture" sqlite3 "${MIG_DB}" "CREATE TABLE sys (key TEXT PRIMARY KEY, value TEXT); INSERT INTO sys(key, value) VALUES ('version','1.0'),('db_version','1.0'); CREATE TABLE files (id INTEGER PRIMARY KEY AUTOINCREMENT, md5 TEXT, audio_md5 TEXT, filepath TEXT, filename TEXT, extension TEXT, filesize INTEGER, last_check_timestamp TIMESTAMP, modified_timestamp INTEGER DEFAULT 0, filetype TEXT DEFAULT 'F', UNIQUE(filepath)); INSERT INTO files(md5,audio_md5,filepath,filename,extension,filesize,last_check_timestamp,modified_timestamp,filetype) VALUES ('0-byte-file','0-byte-file','/legacy/zero.mp3','zero.mp3','mp3',0,0,0,'F'),('Not calculated','Bad audio','/legacy/bad.mp3','bad.mp3','mp3',123,0,0,'F'),('Not calculated','Not calculated','/legacy/unchecked.mp3','unchecked.mp3','mp3',321,0,0,'F');"
run_step "trigger 1.0 -> 1.01 migration" "${ROOT}/fhash" check -s "${WORK}" -r -e mp3 -d "${MIG_DB}"
run_step "migration schema/version checks" bash -lc "sqlite3 '${MIG_DB}' \"PRAGMA table_info(files);\" | grep -q '|audio_check_result|' && sqlite3 '${MIG_DB}' \"SELECT value FROM sys WHERE key='db_version';\" | grep -qx '1.01' && sqlite3 '${MIG_DB}' \"SELECT value FROM sys WHERE key='version';\" | grep -qx '1.01'"
run_step "migration backfill checks" bash -lc "sqlite3 '${MIG_DB}' \"SELECT audio_check_result FROM files WHERE filepath='/legacy/zero.mp3';\" | grep -qx '1' && sqlite3 '${MIG_DB}' \"SELECT audio_check_result FROM files WHERE filepath='/legacy/bad.mp3';\" | grep -qx '3' && sqlite3 '${MIG_DB}' \"SELECT audio_check_result FROM files WHERE filepath='/legacy/unchecked.mp3';\" | grep -qx '4'"

echo "[INFO] Results written to ${OUT}"
