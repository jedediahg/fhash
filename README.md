# fhash

`fhash` is a high-performance command-line tool designed to recursively scan directories, calculate MD5 hashes of files, and store the metadata in a SQLite database. 

A key feature of `fhash` is its ability to calculate the MD5 hash of **audio streams only**, ignoring container-level metadata and tags (like ID3). This allows you to identify duplicate audio content even if the files have different filenames, tags, or bitrates (if re-encoded, though primarily intended for finding the exact same stream data in different containers).

## Features

- **Recursive Scanning**: Efficiently traverses directory trees.
- **SQLite Storage**: Saves file paths, sizes, timestamps, and hashes for easy querying.
- **File Hashing**: Calculates standard MD5 hashes for the entire file.
- **Audio Hashing**: Uses FFmpeg to extract and hash only the audio data, bypassing metadata.
- **Batch Processing**: Uses SQLite transactions for high-speed indexing.
- **Incremental Updates**: Only updates changed files unless forced.

## Prerequisites

To build `fhash`, you need a C compiler (`gcc`) and the following libraries installed on your system:

- **SQLite3**: For database storage.
- **OpenSSL**: For MD5 calculation.
- **FFmpeg (libavformat, libavcodec, libavutil)**: For audio stream processing.

On Debian/Ubuntu-based systems, you can install these with:
```bash
sudo apt-get install build-essential libsqlite3-dev libssl-dev libavformat-dev libavcodec-dev libavutil-dev
```

## Building

Use the provided `makefile` to build the application:

```bash
make
```

This will produce an executable named `fhash`.

## Installation

To install `fhash` system-wide on a *nix system:

```bash
sudo make install
```

This will install the binary to `/usr/local/bin/fhash`. To uninstall:

```bash
sudo make uninstall
```

## Usage

```bash
./fhash scan [options]
./fhash dupe (-xa<n> | -xh<n>) [options]
./fhash link (-xa<n> | -xh<n>) -l{mode} [options]
```

### Options:

- `-help`: Show help text.
- `-v`: Verbose output (default: OFF).
- `scan` options: `-s <startpath>` (default `.`), `-e <extlist>`, `-r`, `-h`, `-a`, `-f`.
- `dupe` options: `-xa<n>` (audio hash) or `-xh<n>` (file hash), optional min group size `n` (default 2).
- `link` options: same as `dupe` plus `-l{mode}` to hard-link duplicates (`s`=shallowest path, `d`=deepest path, `m`=most metadata, `o`=oldest, `n`=newest).
- Shared options (all commands): `-e <extlist>` filter, `-s <startpath>` (with `-r` to include subdirectories) constrain duplicate queries, `-d <dbpath>` (default `./file_hashes.db`), `-v` verbose, `-dry` dry run.

**Duplicate/Link notes**
- `dupe` and `link` commands use existing DB contents; they respect `-s`/`-r`/`-e` as filters on the query. Without `-r`, filtering by `-s` is limited to that directory only.
- `-xa` and `-xh` are mutually exclusive. `-l` is only valid with the `link` command.
- `-dry` is global; in `link` mode it prints planned links without changing files or DB rows.
  
**Examples:**

Scan and hash a music folder:
```bash
./fhash scan -s ~/Music -e mp3,flac -h -a -r
```

List file-hash duplicates (min group 3) under a path:
```bash
./fhash dupe -xh3 -s ~/Music -r
```

Dry-run a link pass using the shallowest path as the keeper, limited to `txt` files:
```bash
./fhash link -xh2 -ls -s ./docs -r -e txt -dry
```

## Database Overview

`fhash` stores results in a SQLite database with two tables:

- `files`: Indexed items and their metadata.
  - `id` (INTEGER PRIMARY KEY AUTOINCREMENT)
  - `md5` (TEXT): Full-file MD5 hash (`Not calculated` if skipped, `0-byte-file` if size was zero).
  - `audio_md5` (TEXT): Audio-only MD5 hash (`Not calculated` if skipped, `0-byte-file` if size was zero, `Bad audio` on FFmpeg/audio errors).
  - `filepath` (TEXT UNIQUE): Absolute path.
  - `filename` (TEXT): Basename of the file.
  - `extension` (TEXT): Extension without dot.
  - `filesize` (INTEGER): Size in bytes.
  - `last_check_timestamp` (TIMESTAMP): Last time `fhash` scanned/linked this entry.
  - `filetype` (TEXT, 1 char): `F` = regular file, `L` = hard link, `D` = directory.
- `sys`: Key/value metadata for the database.
  - `version`: Application version recorded in the DB.
  - `db_version`: Schema version recorded in the DB.

`fhash` initializes `sys` on first run and validates `version`/`db_version` on startup before scanning or duplicate operations.

### Examples:

**Index all MP3 and FLAC files in a folder, calculating both file and audio hashes:**
```bash
./fhash -s ~/Music -e mp3,flac -h -a -r -v
```

**Find duplicate audio content using the database:**
```bash
sqlite3 file_hashes.db "SELECT audio_md5, COUNT(*) c FROM files GROUP BY audio_md5 HAVING c > 1;"
```

## License

This project is intended for personal or educational use.
