<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Live validation gallery

Run `python3 tools/scripts/validation_gallery.py` from this checkout and open
<http://127.0.0.1:8765/>. The page polls every 2.5 seconds, shows captures in
chronological order, follows the latest image by default, filters by filename
or worktree, and opens each original at full size.

The server discovers the main QindaTK checkout and `QindaTK-*` worktrees. It
collects raster images from `build/`, `screenshots/`, and `evidence/` paths, as
well as files named like captures or previews. It deduplicates exact image
bytes and archives each image under
`~/.local/share/qindatk-validation-gallery/archive/`, so later worktree
cleanup does not remove images already shown. The manifest and archive stay
local; the server binds to `127.0.0.1` by default.

Capture order uses each source file's modification timestamp, which is the
capture time for direct `qtk-preview --grab` output. Existing copied files may
only retain their copy time. New captures keep their recorded order even after
their source worktree is removed.
