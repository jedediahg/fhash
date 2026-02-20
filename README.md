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
./fhash [-help] [-v] [-f] [-h] [-a] [-r] [-d <dbpath>] -s <startpath> -e <extensionlist>
```

### Options:

- `-help`: Show help text.
- `-v`: Verbose output (default: OFF).
- `-f`: Force re-indexing of files (updates existing records).
- `-h`: Calculate standard MD5 hash of the entire file.
- `-a`: Calculate MD5 hash of the **audio stream only**.
- `-r`: Recurse into subdirectories.
- `-d <dbpath>`: Path to the SQLite database (default: `./file_hashes.db`).
- `-s <startpath>`: The directory to start scanning from.
- `-e <extensionlist>`: Comma-separated list of extensions to index (e.g., `mp3,flac,wav`).

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
