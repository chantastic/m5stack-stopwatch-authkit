# stb_image

JPEG-only decoder for baseline and progressive public profile avatars.

- Upstream: https://github.com/nothings/stb
- File: https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
- Revision: 2c980bb59875b0d32144a71867fbdebb2f77cd20
- Version: 2.30
- SHA-256: 594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
- License: MIT or public domain; the unmodified header includes both license texts.

The wrapper in ../avatar_decode.h limits input size and dimensions, disables other formats and file I/O, and allocates decoder memory only in PSRAM.
