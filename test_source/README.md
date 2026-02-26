# Test Media Fixtures

This folder holds small, generated MP3 samples used for automated tests:

- Two identical "Hard Link Hearts" files (one labeled "- Copy").
- Two "Hard Link Hearts (take 2)" files with altered metadata but identical audio stream.
- `0Bytes.mp3` — empty file to trigger `0-byte-file` sentinel.
- `BadAudio.mp3` — corrupted/invalid audio to trigger `Bad audio` sentinel.

Files are synthetic and generated solely for testing; no copyrighted material is included.

Subfolder `other_songs/` contains extra generated samples and is intentionally ignored by tests and Git tracking.
