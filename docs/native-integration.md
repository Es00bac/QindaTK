<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Native windows and desktop integration

`WindowHost` owns the mapping between live `QQuickWindow` instances and
`DocumentManager` document/view IDs. `createWindow(documentId, viewId, role,
transientParent)` creates a native window; `registerWindow` attaches an
application-created window. The host never owns a registered external window.
An omitted view ID takes an unused document view or duplicates one. The same
view cannot be registered twice. `windows` reports document/view IDs, logical
geometry, screen name, device-pixel ratio, visibility and activation.

Closing a nonfinal view removes that view from `DocumentManager`. Closing the
last dirty view requires an explicit discard decision or a completed save.
While a save is in progress, close is rejected. A native close event follows
the same guard as `closeWindow`; externally destroying a registered window
releases its view. Applications should run their own save/close prompt in
response to `closeBlocked`, then call `closeWindow` again after the decision.
`moveWindow` clamps logical geometry to an available screen; an unknown screen
ID fails without moving the window. `reconcileScreens` moves unreachable
windows to the primary screen. Mixed-DPI and real monitor removal are platform
qualification tasks beyond the single offscreen screen in CI.

`FileDialogAdapter` accepts `open`, `openMultiple`, `save` and `folder` requests
with MIME filters, an initial URL, title and parent `QWindow`. It returns a
request ID and emits `finished(id, {status, urls, message})`, where status is
`accepted`, `canceled`, `error` or `unsupported`. `cancel` emits one terminal
result; a late backend callback is ignored. The adapter checks selected local
locations again on completion, including a missing/revoked path. Remote URLs
are unsupported by the shipped backend. Without an injected backend, requests
return `unsupported` rather than opening a styled fallback dialog.

The optional `QindaTKWidgets` CMake package supplies
`QindaTK::qindatk_widgets` and `NativeFileDialogBackend`, a Qt `QFileDialog`
implementation. It requires `QApplication` and Qt Widgets. Core
`find_package(QindaTK)` and QML controls have no Widgets dependency. Inject
the backend from C++:

```cpp
#include <file_dialog_adapter.h>
#include <native_file_dialog_backend.h>

QindaTK::FileDialogAdapter dialogs;
dialogs.setBackend(std::make_shared<QindaTK::NativeFileDialogBackend>());
```

`DesktopIntegration` exposes application-local recent URLs, `openUrl`,
`openWith`, `reveal`, window activation and external `QFileOpenEvent` requests.
Call `capabilities()` before showing an operation. The current core adapter
does not register OS recent files, intercept second-instance launches or
promise an OS reveal operation outside the Linux `xdg-open` path. These are
reported as unsupported capability values. Its recent list is in memory;
an application can persist it through `SettingsModel`.

Native behavior was tested on Linux/offscreen Qt 6.8.3 and 6.11.1. Real
window-manager focus policy, mixed-display scaling, actual monitor removal,
macOS/Windows desktop affordances and portal grants still require platform
qualification.
