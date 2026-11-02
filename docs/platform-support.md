<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Platform and Qt support matrix

This is the WP-005 qualification record as of 2026-09-23. A configuration is
supported only for the operations actually exercised below. The minimum in
`CMakeLists.txt` is Qt 6.8; both Qt 6.8.3 and 6.11.1 have now passed the
facade and isolated installed-consumer gates. [WP-005 evidence](universal-toolkit/implementation/evidence/WP-005.md)
records the commands and results.

| Host and Qt | Build / QML | Rendering | Input | Installed C++ consumer | Status |
| --- | --- | --- | --- | --- | --- |
| Gentoo Linux, Qt 6.11.1 | Debug integration build, latest 16/16 CTest targets and `all_qmllint` target passed | Headless offscreen software; focused SectionHeader compact/comfortable captures inspected | Synthetic pen, eraser, mouse and touch delivery in `tst_stylus_handler`; no physical tablet run | New facade staged, clean consumer configured/built and loaded QML; no private Qt include/link dependency | Verified for these paths |
| Gentoo Linux, isolated Qt 6.8.3 SDK | Debug build, latest 16/16 CTest targets and `all_qmllint` target passed; optional QindaQt desktop bridge disabled | Headless offscreen software and example instantiation passed | Synthetic pen, eraser, mouse and touch test passed; no physical tablet run | New facade staged, clean consumer configured/built and loaded QML with SDK library path; no private Qt include/link dependency | Verified for these paths |
| Gentoo Linux, Qt 6.11.1 on Wayland/GPU | Wayland session and render device present | Direct GPU and multi-monitor behavior not qualified here | Physical pen/assistive devices not qualified | — | Later WP-088/092 gates |
| Android arm64-v8a (API 28+), Qt for Android 6.11.1, NDK 29 | Release library and QML module via `cmake --preset android-arm64`; a consumer app built against the staged install and an in-tree smoke APK ([android.md](android.md), [D-354](decisions.md#d-354--android-arm64-is-a-supported-target-desktop-only-backends-are-off-there)) | Smoke app run and inspected on a physical phone (SM-S948U, Android 17); SQLite, lcms2, libvterm, the CLAP host and the other desktop-only backends are off | No physical-keyboard, IME, stylus or accessibility run | Staged install configured, built and bundled by `androiddeployqt` | Verified for the smoke controls on one device |
| Windows and macOS | Not built | Not run | Not run | Not run | No support claim |

The installed public `stylus_handler.h` now exposes a `QObject` facade.
The Qt private pointer handler lives in its `.cpp`, and `Qt6::QuickPrivate`
is a private build dependency. The staged package's CMake config and target
do not export it; both consumer compiler commands omit private Qt include
paths. [D-015](decisions.md#d-015--keep-private-qt-behind-a-public-stylus-facade)
records the boundary and migration.

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

Configure against the SDK with only `-DCMAKE_PREFIX_PATH=<sdk>/gcc_64`; no
`CMAKE_DISABLE_FIND_PACKAGE_*` flags are needed. Every Qt module beyond the
required core set (Quick3D, ShaderTools, Widgets, PrintSupport, and
QuickPrivate where it is a separate package) goes through
`qindatk_find_qt_module()`, which searches only the directory holding
`Qt6_DIR` at exactly `Qt6_VERSION`
([D-199](decisions.md#d-199--qt-modules-come-only-from-the-qt-in-use)). The
SDK has no Quick3D or Shader Tools, so configure reports the SceneView3D
renderer and the GPU volume shaders unavailable instead of loading the system
Qt's module configs. A module switched off with its `QINDATK_ENABLE_*` option
([D-201](decisions.md#d-201--optional-engines-are-explicit-build-switches-with-a-resolved-feature-summary))
is not searched at all; `qindatk-features.json` in the build directory records
what was resolved.

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
Existing QML sample-stream usage is retained by the tested facade. The
inherited `dragThreshold` and `cursorShape` properties of the former private
base are not proxied. Code using them must migrate, as must C++ casts to the
former base. Physical tablet and native assistive behavior remain separate
qualification work.

## WP-088 qualification harness

From WP-088 on, Linux claims come from `tools/qualify/qualify.py`
([qualification.md](qualification.md)). It covers:

- Qt 6.11 and the Qt 6.8 SDK;
- offscreen, Wayland (headless Weston) and xcb (private Xwayland), with the
  software, OpenGL and Vulkan scene graphs;
- every optional engine omitted, one at a time;
- install relocation.

The latest recorded result table is
[evidence/WP-088-results.md](universal-toolkit/implementation/evidence/WP-088-results.md).
A configuration or operation marked unqualified there is not claimed.
