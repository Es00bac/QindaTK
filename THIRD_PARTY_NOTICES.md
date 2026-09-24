# Third-party notices

## Lucide icons (built-in icon set)

`src/core/icon_data.cpp` is generated from the Lucide icon package
(https://lucide.dev, version 1.8.0) by `tools/scripts/gen_icons.py`.

ISC License

Copyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as part
of Feather (MIT). All other copyright (c) for Lucide are held by Lucide
Contributors 2022-2026.

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.

## FFmpeg libraries (optional media backend)

When configured with `QINDATK_ENABLE_FFMPEG` (the default) and the libraries
are found, `qindatk` links dynamically to the system's FFmpeg libraries
(`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`,
https://ffmpeg.org). QindaTK ships no FFmpeg code. FFmpeg is licensed under
the LGPL 2.1 or later, or the GPL 2 or later when built with GPL components;
a distributor of an FFmpeg-enabled build must meet the terms of the FFmpeg
configuration it ships (see decision D-096).
