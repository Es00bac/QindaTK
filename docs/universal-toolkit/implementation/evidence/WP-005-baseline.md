<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-005 — Private Qt and installed-consumer baseline

Status: **baseline only; WP-005 active**. Luna prepared an isolated installed
consumer fixture in commit `3a0989c`; Sol integrated it as `9727172`.
The source refactor and staged consumer repeat on both Qt versions remain open.

## Observed exposure

The installed `<qindatk/stylus_handler.h>` includes Qt Quick's private
`qquickpointerdevicehandler_p.h` and derives from that private class.
Installed `QindaTKTargets.cmake` exports `Qt6::QuickPrivate` in
`INTERFACE_LINK_LIBRARIES`, and `QindaTKConfig.cmake` explicitly calls
`find_dependency(Qt6QuickPrivate)`. `LayoutInfo`'s public header has no
private Qt include; its implementation is private to the library.

## Clean consumer reproduction

The standalone [consumer fixture](../../../../tests/consumer/stylus/README.md)
uses `find_package(QindaTK 0.1 CONFIG REQUIRED)`, links only
`QindaTK::qindatk`, includes the installed stylus header, constructs the
C++ type and loads a QML scene containing `Tk.StylusHandler`. It has no
source-tree include path or build-tree library link.

On this Gentoo host, `qmake6 -query QT_VERSION` and `pkg-config` reported
Qt 6.11.1. The worker read `portageq envvar MAKEOPTS` as `-j24 -l24` and
ran `ninja -C build/consumer-stylus -j24 -l24`; configure, build and
`QT_QPA_PLATFORM=offscreen build/consumer-stylus/qindatk_stylus_consumer`
all exited 0. CMake emitted a Qt private-module compatibility warning.
Sol independently configured the same fixture in `build/consumer-pre`,
read the live `MAKEOPTS` as `-j24 -l24`, built with
`ninja -C build/consumer-pre -j24 -l24`, and ran the offscreen executable;
all three steps exited 0. The configure warning identified the imported
`Qt6QuickPrivate` dependency through the installed QindaTK config.

The original installed Qt package inventory and searches under `/usr`,
`/opt` and `/usr/local` found only Qt 6.11.1. Sol subsequently acquired an
isolated official Qt 6.8.3 SDK in the ignored integration build tree and
ran the existing toolkit baseline against it, as detailed below. The facade
design is recorded in [D-015](../../../decisions.md#d-015--keep-private-qt-behind-a-public-stylus-facade);
only a future facade build and staged installed-consumer run can qualify the
new boundary.

## Isolated Qt 6.8.3 baseline

- Installed `aqtinstall` 3.3.0 into `build/qt68-tools` and fetched the
  official Qt 6.8.3 Linux GCC 64 archives to ignored `build/qt68-sdk`.
  The first extraction failed in aqt's external-7z path with a Python
  `TypeError`; an internal-extractor retry for qtbase completed the SDK.
  This did not change system Qt, Portage configuration or `MAKEOPTS`.
- `qmake6 -query QT_VERSION` from that SDK returned `6.8.3`. The private
  `qquickpointerdevicehandler_p.h` is present, and the SDK's
  `Qt6QuickTargets.cmake` declares `Qt6::QuickPrivate`. It lacks the
  distro-specific standalone `Qt6QuickPrivateConfig.cmake`; the root CMake
  build now falls back to that config only if the target is not already
  present.
- Configured `build/qt68` with the SDK's `CMAKE_PREFIX_PATH` and `Qt6_DIR`.
  The optional qtk-preview QindaQt desktop theme bridge initially linked
  host Qt 6.11 libraries and failed with undefined Qt 6.11 symbols. It was
  disabled via `QINDATK_PREVIEW_QINDAQT=OFF` for the isolated Qt 6.8 build;
  the main toolkit, QML plugin, preview and tests then built successfully.
- Read actual `portageq envvar MAKEOPTS` as `-j24 -l24` and built with
  `ninja -C build/qt68 -j24 -l24`. `ldd` on `tst_stylus_handler` resolves
  Qt Core, Gui, Qml, Quick and Test libraries from the 6.8.3 SDK path.
  `ctest --test-dir build/qt68 --output-on-failure`: **15/15 passed** on
  the existing pre-facade source, including the synthetic pen path.

This is a baseline result, not the final WP-005 acceptance run. It does
not test physical tablet hardware, GPU rendering, the optional desktop
bridge against Qt 6.8, or the new installed public facade.
