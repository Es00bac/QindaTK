<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-005 — Private Qt and installed-consumer baseline

Status: **baseline only; WP-005 active**. Luna prepared an isolated installed
consumer fixture in commit `3a0989c`; Sol integrated it as `9727172`.
The source refactor, staged consumer repeat and Qt 6.8 run remain open.

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

The installed Qt package inventory and searches under `/usr`, `/opt` and
`/usr/local` found only Qt 6.11.1. The declared minimum Qt 6.8 has not been
compiled or run. The facade design is recorded in [D-015](../../../decisions.md#d-015--keep-private-qt-behind-a-public-stylus-facade);
only future staged installed-consumer and version runs can qualify it.
