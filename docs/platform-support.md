<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Platform and Qt support matrix

This is the initial WP-005 qualification record as of 2026-09-23. A
configuration is supported only for the operations actually exercised below.
The minimum in `CMakeLists.txt` is Qt 6.8; it is a build requirement, not yet
a verified runtime claim. [WP-005 evidence](universal-toolkit/implementation/evidence/WP-005-baseline.md)
records the installed-consumer reproduction.

| Host and Qt | Build / QML | Rendering | Input | Installed C++ consumer | Status |
| --- | --- | --- | --- | --- | --- |
| Gentoo Linux, Qt 6.11.1 | Debug integration build and all 15 CTest targets passed | Headless offscreen software; compact/comfortable captures inspected | Synthetic pen, eraser, mouse and touch delivery in `tst_stylus_handler`; no physical tablet run | Existing installed package consumer configured, built and loaded QML; it still inherits `QuickPrivate` | Verified only for these paths |
| Gentoo Linux, Qt 6.8.x | No local Qt 6.8 install found | Not run | Not run | Not run | Open WP-005 gate |
| Gentoo Linux, Qt 6.11.1 on Wayland/GPU | Wayland session and render device present | Direct GPU and multi-monitor behavior not qualified here | Physical pen/assistive devices not qualified | — | Later WP-088/092 gates |
| Windows and macOS | Not built | Not run | Not run | Not run | No support claim |

The current installed public `stylus_handler.h` derives from a Qt Quick
private class. `QindaTK::qindatk` consequently exports
`Qt6::QuickPrivate` today. [D-015](decisions.md#d-015--keep-private-qt-behind-a-public-stylus-facade)
selects an internal-handler/public-facade boundary. Its effect on the
installed package must be rechecked after implementation; this matrix does
not report that pending change as verified.

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
