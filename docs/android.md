<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Android (arm64-v8a)

QindaTK builds for Android arm64-v8a against Qt for Android 6.11 and runs real
controls on a device ([D-354](decisions.md#d-354--android-arm64-is-a-supported-target-desktop-only-backends-are-off-there)).
This page is the build recipe, the consumer recipe the QindaLink phone app
follows, and the list of what is switched off.

## Toolchain (this workstation)

| Part | Location |
| --- | --- |
| Qt for Android 6.11.1 | `/opt/qt6-android/6.11.1/android_arm64_v8a` (host tools in `/opt/qt6-android/6.11.1/host/bin`) |
| Android SDK / NDK | `/opt/android-sdk`, NDK `29.0.14206865` |
| JDK | `/opt/openjdk-bin-21` (export `JAVA_HOME`; Gradle needs it) |
| Gradle | the wrapper Qt's APK template uses (9.3.1) |

The host Qt must be the same version as the Android Qt (6.11.1).

## Build the library and QML module

```
export JAVA_HOME=/opt/openjdk-bin-21
cmake --preset android-arm64          # build/android-arm64, Release, Ninja
cmake --build --preset android-arm64  # libqindatk, the QML plugins, the smoke APK
cmake --install build/android-arm64   # stage: build/android-arm64/stage
```

The preset points `CMAKE_TOOLCHAIN_FILE` at Qt's `qt.toolchain.cmake` (override
with `-DCMAKE_TOOLCHAIN_FILE=` for another Qt prefix) and switches off every
desktop-only backend. No `-j` is passed; Ninja uses the configured parallelism.

The stage holds `lib/libqindatk_arm64-v8a.so`, `lib/qml/QindaTK/` (the QML
module: `qmldir`, `.qmltypes`, `.qml`, and the plugin
`libqml_QindaTK_qindatkplugin_arm64-v8a.so`), `lib/libqindatk_agent.a`,
`include/qindatk/` and `lib/cmake/QindaTK/`. Release builds keep the NDK's `-g`;
`androiddeployqt` strips libraries when it packages the APK.

## Consumer recipe (an app that uses `import QindaTK as Tk`)

Configure the app with Qt's Android toolchain and give it the stage **both** as
`CMAKE_PREFIX_PATH` and `CMAKE_FIND_ROOT_PATH` (the Android toolchain restricts
`find_package` to the find-root):

```
STAGE=/path/to/qindatk/stage
/opt/qt6-android/6.11.1/android_arm64_v8a/bin/qt-cmake -G Ninja -S app -B build/app \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$STAGE -DCMAKE_FIND_ROOT_PATH=$STAGE
```

In the app's `CMakeLists.txt`:

```
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
find_package(QindaTK REQUIRED)
qt_add_executable(app main.cpp)
qt_add_qml_module(app URI MyApp VERSION 1.0 QML_FILES Main.qml)
target_link_libraries(app PRIVATE QindaTK::qindatk Qt6::Quick Qt6::QuickControls2)
# androiddeployqt bundles QML plugins only from the import paths it is given.
set_property(TARGET app APPEND PROPERTY QT_QML_IMPORT_PATH "${QindaTK_QML_IMPORT_PATH}")
```

Rules that matter:

- `QindaTK_QML_IMPORT_PATH` (from the package config) is the directory that
  *contains* the `QindaTK` module (`<stage>/lib/qml`). Without the
  `QT_QML_IMPORT_PATH` line the APK gets `libqindatk` but not the QML plugin,
  and `import QindaTK` fails at run time.
- `libqindatk_arm64-v8a.so` is picked up through the imported target's library
  directory (`extraLibraryDirs` in the deployment settings). It is a shared
  library plus a separate plugin; nothing needs static linking, and
  `Q_IMPORT_QML_PLUGIN` is not needed because the plugin is loaded from the
  APK's `lib/arm64-v8a` by the QML engine.
- Link `Qt6::QuickControls2` and call `QQuickStyle::setStyle("Basic")` before
  loading QML. The controls are `QtQuick.Templates` with QindaTK visuals, so
  only the style's existence matters, not its look. The default Material style
  also works but adds nothing.
- Set theme and density from QML, so the app needs no QindaTK C++ header:
  `Tk.Theme.applyPreset("sloom-dark")`, `Tk.Density.mode = Tk.Density.Touch`.
  A C++ app includes `<qindatk/theme.h>` (installed headers live in
  `include/qindatk/`).
- The window is edge to edge on current Android. Keep content clear of the
  system bars with `win.SafeArea.margins` (Qt 6.9+), as the smoke app does.
- Reinstalling over a build signed with a different debug key fails with
  `INSTALL_FAILED_UPDATE_INCOMPATIBLE`; uninstall your own package first.
- Signing: `androiddeployqt` produces an unsigned, `zipalign`ed-if-asked APK.
  Debug-sign it with `apksigner` and a keystore generated under the build
  directory (`keytool -genkeypair ...`); never commit keys.

## No scripts in the Android qmldir

`import QindaTK` fails on a device with `Script qrc:/android_rcc_bundle/qml/QindaTK/X.js
unavailable` if the qmldir lists a `.js` file. androiddeployqt's `android_rcc_bundle`
carries only `qmldir` and the typeinfo; components are served from the library's
compiled resources through `prefer :/qt/qml/QindaTK/`, but the engine looks a
script entry up in the bundle. The module's own QML imports scripts by relative
path, so the Android build skips the qmldir script entries
([D-355](decisions.md#d-355--android-qmldir-files-list-no-scripts-a-build-guard-checks-the-module)).
The `qindatk_check_android_bundle` target (`cmake/CheckQmlModuleBundle.cmake`) runs in
every Android build and fails if the qmldir names a missing file or any `.js`/`.mjs`.

## The smoke app

`examples/android-smoke` (package `org.qindatk.smoke`) is built by the preset
above as `qindatk-android-smoke` and also builds standalone against a stage
(the consumer recipe). `AndroidSmoke.qml` shows `Tk.Theme`, `Tk.Flex`/`Tk.Grid`
layout, `Tk.Heading`, `Tk.Caption`, `Tk.Panel`, `Tk.TextField`, `Tk.Switch`,
`Tk.Button` and a `Tk.VirtualList`. Its objectNames (`smokeRoot`, `smokePanel`,
`smokeName`, `smokeSwitch`, `smokePair`, `smokeList`) are stable.

## What is off on Android

| Switch | Android | Effect |
| --- | --- | --- |
| `QINDATK_ENABLE_SQLITE` | OFF | The NDK has no public SQLite. `LocalDatabaseProvider` keeps its API but never connects; report/data-design/query consumers take their "not connected" path. |
| `QINDATK_ENABLE_LCMS2` | OFF | `ColorProfile`/`ColorEngine` keep their API; profiles are invalid and `convert` returns `ErrorCode::Unavailable`. |
| `QINDATK_ENABLE_LIBVTERM` | OFF | `TerminalSession` keeps its API; `start()` fails with an explanatory `errorString`. Defines `QINDATK_NO_LIBVTERM` publicly. |
| `QINDATK_ENABLE_PLUGIN_HOST` | OFF | No `qindatk-plugin-host` executable (Android cannot exec a sibling binary); CLAP scans and instances report a missing host. |
| `POPPLER_QT6`, `ALSA`, `FFMPEG`, `DBUS`, `WIDGETS`, `NATIVE_PRINT` | OFF | Existing optional backends, as on any build without them. |
| `QUICK3D`, `SHADER_TOOLS` | OFF in the preset | Installed in this Qt, but a phone UI does not need SceneView3D or GPU volume shaders. Switch on to try. |

The replacements are `core/color_unavailable.cpp`,
`core/local_database_provider_unavailable.cpp` and
`core/terminal_session_unavailable.cpp`; `src/CMakeLists.txt` swaps them in.
`qindatk-features.json` records `sqlite`, `lcms2`, `libvterm` and `pluginHost`.

## Qt for Android here has no SSL

The installed Qt for Android was configured without SSL (`QT_NO_SSL`;
`QSslSocket` is only a forward declaration and the only TLS backend is
`qcertonly`). `Protocol::Channel` (IMAP/POP3/SMTP) therefore uses a plain
`QTcpSocket` on Android and refuses `tls`/`starttls` endpoints with a rejected
failure. Anything that needs TLS on the phone (QindaLink's pinned-TLS transport)
must bring its own TLS implementation or a Qt rebuilt with OpenSSL.

## Source compatibility fixes made for the NDK

- `EngineSchedule` publishes through `EngineScheduleSlot`, which falls back to
  `std::atomic_load/store` on `shared_ptr` where libc++ lacks
  `std::atomic<std::shared_ptr>`.
- `AudioPluginProcessor` creates its shared block with `syscall(SYS_memfd_create)`
  (bionic declares `memfd_create` from API 30; the minimum here is 28).

## Not qualified

The smoke app ran on a physical phone (Samsung SM-S948U, Android 17, 1080x2340) and
its screen was inspected: heading and caption, the Pairing panel (text field with
placeholder, accent-on Switch, Cancel and accent Pair buttons), the Devices section
header with its count and a four-row `VirtualList` whose Lucide icons all drew. Text
and glyphs were crisp, Touch density gave roughly 48 dp controls that read as
finger-sized, and the content sat below the status bar and above the three-button
navigation bar, so the `SafeArea.margins` insets work. Not exercised: keyboard and
IME entry into the field, scrolling, rotation, dark/light system switching.
Tests are desktop-only
(`BUILD_TESTING=OFF` in the preset); no Android instrumentation, input-method,
accessibility, GPU or multi-window claim is made.
