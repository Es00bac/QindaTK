<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Handoff: Android qmldir script entries (wp/android-js-bundle)

- Base: QindaTK main `27da0a0d`. Fix and docs in the commit that adds this file.
- Root cause: `qt_add_qml_module` listed each `.js` in the qmldir; androiddeployqt's
  `android_rcc_bundle` holds only qmldir and typeinfo, and the engine resolves script entries
  against that bundle path (not `prefer`), so `import QindaTK` failed at run time. Scripts are only
  imported by relative path inside the module, so Android skips their qmldir entries
  (`QT_QML_SKIP_QMLDIR_ENTRY`, `src/CMakeLists.txt`); desktop qmldir unchanged. D-355.
- Guard: `cmake/CheckQmlModuleBundle.cmake` via target `qindatk_check_android_bundle` (ALL, Android only):
  fails on a qmldir file missing from the module dir or any .js/.mjs entry. Verified failing on the old
  stage (nine .js named) and passing in the new Android build.
- Smoke app: `NO_RESOURCE_TARGET_PATH` removed (it hid AndroidSmoke from `loadFromModule`; the app died
  with "Module QindaTKSmoke contains no type named AndroidSmoke").
- Device (R3GL40ABQXM, unlocked, own app foreground): `org.qindatk.smoke` built as a staged-install
  consumer, installed, launched, no QML errors in the app's log; screencap (cropped to the app area,
  `build/smoke-device-app.png`, ignored) inspected, see docs/android.md.
- Desktop: `cmake -S . -B build/dev-check -G Ninja` configures; the change is Android-conditional.
- Caveat: the old install used a different debug key, so my own package was uninstalled and
  reinstalled. The smoke app is left installed.
