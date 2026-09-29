<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Gentoo packaging

`dev-libs/qindatk/` mirrors the package in the `qindaqt` overlay
(QindaGentoo, hub `qinda:~/git/QindaGentoo.git`). Each revision pins
`EGIT_COMMIT` to a commit on the hub; older revisions stay as immutable pins.
Copy a new revision here and into the overlay together.

Revision `0.1.0-r13` pins the live-agent SDK (D-306): the installed
`QindaTK::agent` static target and headers plus `qinda-agent` CLI/MCP client.
It adds a Python 3 runtime dependency; the client uses only its standard
library. Existing toolkit/QML APIs remain compatible.

Revision `0.1.0-r6` is the first pin of the finished universal toolkit and
declares every library the build uses:

| Always | Why |
|---|---|
| `dev-qt/qtbase[concurrent,gui,network]`, `qtdeclarative`, `qtsvg` (>= 6.8, slot operator: QuickPrivate ties the build to one Qt) | toolkit and QML module |
| `dev-db/sqlite:3` | project, mail and notebook stores |
| `media-libs/lcms:2` (>= 2.14) | numerical colour engine (WP-043) |
| `dev-libs/libvterm` (>= 0.3) | terminal emulation; the CMake package config asks consumers for it too |
| `dev-qt/qtshadertools` (build) | GPU volume raycasting shaders (D-147) |

| USE flag | CMake switch | Adds |
|---|---|---|
| `alsa` | `QINDATK_ENABLE_ALSA` | real PCM devices (`media-libs/alsa-lib`) |
| `ffmpeg` | `QINDATK_ENABLE_FFMPEG` | media decode/encode (`media-video/ffmpeg`, D-096) |
| `pdf` | `QINDATK_ENABLE_POPPLER_QT6` | PDF rendering (`app-text/poppler[qt6]`) |
| `quick3d` | `QINDATK_ENABLE_QUICK3D` | 3D views (`dev-qt/qtquick3d`) |
| `widgets` | `QINDATK_ENABLE_WIDGETS` | native file dialog adapters (`qtbase[widgets]`) |
| `cups` | `QINDATK_ENABLE_NATIVE_PRINT` | native print backend with CUPS (`net-print/cups`); needs `widgets` |
| `qindaqt` | `QINDATK_PREVIEW_QINDAQT` | `qtk-preview --qst` (`gui-wm/qindaqt-desktop`, which itself depends on qindatk: disable for a first install) |
| `examples` | `QINDATK_BUILD_EXAMPLES` | QML examples under `/usr/share/qindatk` |
| `test` | `BUILD_TESTING` | headless suite in `src_test` |

The vendored CLAP headers (`third_party/clap`) are MIT, so `LICENSE` carries
`LGPL-3+ ISC MIT`. Portage's configured MAKEOPTS apply unchanged.
