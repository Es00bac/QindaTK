<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Gentoo packaging

`dev-libs/qindatk/` mirrors the package in the `qindaqt` overlay
(QindaGentoo, hub `qinda:~/git/QindaGentoo.git`). Historical git-r3 revisions pin
`EGIT_COMMIT` to a commit on the hub. Revision16 uses a fetch-restricted
source archive with the exact same hub provenance; older recipes remain immutable.
Copy a new revision here and into the overlay together.

Revision `0.1.0-r16` pins reviewed source
`4fb7ae7a3f08140b9cb5085eba22cf5901e2f4ab` and delivers the additive scoped Read
SDK (D-308). The existing `QindaTK::agent` static archive installs
`scoped_agent_host.h`, `scoped_agent_policy.h` and `scoped_agent_types.h`, and
exports C++20 to consumers. Legacy agent headers/library, CLI and v1 protocol
retain their source behavior. Runtime dependencies, explicit build dependency
declarations, license, slot and USE switches remain those of revision15. Removing git-r3 also removes its
inherited Git fetch-tool build dependency; consumers of the additive static API
rebuild against matching headers. This package does not supply authenticated desktop
principal/privacy composition or grant installed applications access.

The new distfile is `qindatk-0.1.0-r16.tar.gz`, with prefix
`QindaTK-4fb7ae7a3f08140b9cb5085eba22cf5901e2f4ab/`. Cut from the exact commit:

```sh
git -c tar.umask=022 archive --format=tar \
  --prefix=QindaTK-4fb7ae7a3f08140b9cb5085eba22cf5901e2f4ab/ \
  4fb7ae7a3f08140b9cb5085eba22cf5901e2f4ab | gzip -n > qindatk-0.1.0-r16.tar.gz
```

Explicit tar modes normalize ordinary files to 0644 and executables to 0755,
preserving Git executable bits and symbolic-link bytes. `gzip -n` removes
filename/time headers. Require two identical cuts, all tracked blob/mode checks
and actual Manifest digests before review. Never recut different source under
this filename, edit historical git-r3 pins, or install a home build.

The prepared archive is 35,149,766 bytes, SHA256
`a729961a664f305323cd1624e7d19f1bbd1281a576a0c91f902f64c0dedfc3ed`;
two cuts match and all 2,919 Git blobs/modes match the frozen source. This is
recipe/source preparation, with no installed or signed-artifact claim.

Recipe/source/archive review precedes a signed build-only Portage package;
package-image review must prove these public headers, actual archive symbols,
`QindaTK::agent` export with C++20, relocated scoped consumer, missing-header
failure and unchanged legacy consumer. Installation/publication and real
OS-authenticated desktop use are separate manager gates. Source acceptance or
an injected SDK fixture alone does not qualify those pending boundaries.

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
