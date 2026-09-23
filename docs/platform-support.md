<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Platform and Qt support matrix

This is the initial WP-005 qualification record as of 2026-09-23. A
configuration is supported only for the operations actually exercised below.
The minimum in `CMakeLists.txt` is Qt 6.8; an isolated Qt 6.8.3 build and
test now qualify the existing baseline, while the new facade and installed
consumer remain open. [WP-005 evidence](universal-toolkit/implementation/evidence/WP-005-baseline.md)
records the installed-consumer reproduction.

| Host and Qt | Build / QML | Rendering | Input | Installed C++ consumer | Status |
| --- | --- | --- | --- | --- | --- |
| Gentoo Linux, Qt 6.11.1 | Debug integration build and all 15 CTest targets passed | Headless offscreen software; compact/comfortable captures inspected | Synthetic pen, eraser, mouse and touch delivery in `tst_stylus_handler`; no physical tablet run | Existing installed package consumer configured, built and loaded QML; it still inherits `QuickPrivate` | Verified only for these paths |
| Gentoo Linux, isolated Qt 6.8.3 SDK | Debug build and all 15 CTest targets passed with optional QindaQt desktop bridge disabled | Headless offscreen software and example instantiation passed | Synthetic pen, eraser, mouse and touch test passed; no physical tablet run | New facade/staged consumer not run yet | Baseline verified; WP-005 boundary gate open |
| Gentoo Linux, Qt 6.11.1 on Wayland/GPU | Wayland session and render device present | Direct GPU and multi-monitor behavior not qualified here | Physical pen/assistive devices not qualified | — | Later WP-088/092 gates |
| Windows and macOS | Not built | Not run | Not run | Not run | No support claim |

The current installed public `stylus_handler.h` derives from a Qt Quick
private class. `QindaTK::qindatk` consequently exports
`Qt6::QuickPrivate` today. [D-015](decisions.md#d-015--keep-private-qt-behind-a-public-stylus-facade)
selects an internal-handler/public-facade boundary. Its effect on the
installed package must be rechecked after implementation; this matrix does
not report that pending change as verified.

## Isolated Qt 6.8 test environment

The [official Qt 6.8.3 Linux GCC 64 repository](https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/)
publishes an isolated desktop SDK component. `aqtinstall` 3.3.0 fetched its
seven official archive payloads into the ignored `build/qt68-sdk` directory
without touching system Qt or Portage. The first external 7z extraction
attempt failed in aqt; retrying qtbase with its internal extractor completed
the SDK. The Desktop payload metadata lists about 193 MiB compressed and
the extracted SDK occupies about 1.6 GiB here.
The [Qt 6.8 Linux configuration list](https://doc.qt.io/qt-6.8/linux.html)
does not include this Gentoo host. The successful headless run establishes
this configuration's tested paths, not general distro certification. The
official SDK contains the private pointer-handler header and defines
`Qt6::QuickPrivate` from `Qt6QuickTargets.cmake`; it has no separate
`Qt6QuickPrivateConfig.cmake`. The top-level build now accepts either Qt
package layout.

## Qt upgrade and qualification procedure

1. Install a matching Qt runtime and private development package in an
   isolated prefix for the target Qt minor version. The internal handler
   and `LayoutInfo` compile against private Qt headers, so rebuild QindaTK
   when changing that version.
2. Configure a fresh QindaTK build directory against the selected prefix.
   Read `portageq envvar MAKEOPTS` and use the configured job **and** load
   limits for direct builds, without overriding Portage settings.
3. Run the C++/QML suite and `all_qmllint`. Run the stylus driver-path and
   layout tests on both the declared minimum and the current development
   version; record exact versions and commands.
4. Install to an isolated staging prefix. Configure the standalone
   `tests/consumer/stylus` project using only that installed package;
   compile its public-header C++ path, construct the QML handler, and inspect
   exported CMake link dependencies and compiler include paths for private
   Qt leakage.
5. Qualify display, GPU, physical input and assistive behavior separately
   before widening those claims. Record failures and skipped environments
   explicitly, then update the matrix and package metadata.

The facade changes the pre-1.0 C++ class ABI. Applications using the C++
handler must rebuild; code that upcasts to its old private pointer-handler
base or subclasses it must migrate to the public facade and its signals.
Existing QML sample-stream usage is the compatibility target, subject to
the WP-005 regressions.
