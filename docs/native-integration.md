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
request ID and emits `finished(id, {status, urls, encodedUrls, message})`, where
status is `accepted`, `canceled`, `error` or `unsupported`. `urls` contains
typed `QUrl` values; `encodedUrls` contains their `FullyEncoded` URI strings
for QML and persistence. The latter avoids Qt-version differences in QML's
`QUrl.toString()` conversion. `cancel` emits one terminal
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

## Desktop global menu

Every `Tk.MenuBar` publishes its menus to the desktop's global menu (D-361).
Applications write an ordinary `Tk.AppWindow { menuBar: Tk.MenuBar { … } }`;
there is no property, environment switch or build option that turns the export
off. `Tk.GlobalMenuExport` (owned by the bar) does the work:

1. One event-loop turn after the bar's window is shown it registers a
   `com.canonical.dbusmenu` object at `/org/qindatk/GlobalMenu/<n>` on the
   session bus, projected live from the bar: menu titles, items, submenus,
   separators, `checkable`/`checked`, `radio`, `enabled`, the application's own
   `visible`, mnemonics (`&File` → `_File`) and the `shortcut` display text.
   The "»" overflow menu is replaced by the menus it holds. Custom items that
   are not `MenuItem`/`MenuSeparator`, items without text and untitled menus
   are not exported; trees are bounded to the panel's limits (depth 6, 128
   children, 1024 items), so one item can never make the panel refuse the menu.
2. It announces the object for the window: on Wayland through Qt's KDE
   app-menu hook (`QDesktopUnixServices::registerDBusMenuForWindow`), on X11
   with `com.canonical.AppMenu.Registrar.RegisterWindow`. Surface destruction
   withdraws the announcement and surface recreation republishes it.
3. It asks the QindaQt registrar `IsMenuHosted(service, path)` and follows
   `MenuHostedChanged`. Only a confirmed `true` sets `hosted`; the bar then
   collapses to zero height and its Alt mnemonics stand down. A click in the
   panel calls the item's `click()`, so `onTriggered`, actions and checkable
   state behave exactly as for a pointer click.

Everything else is fail-closed and keeps the in-window bar: no session bus,
no registrar, a platform without window announcements (offscreen, eglfs), a
QindaTK build without Qt DBus (`status` `unavailable`), or the panel not
showing the menu. Exactly one bar exports per window (`duplicate` for the
rest). An application that publishes its desktop menu itself — any
`com.canonical.dbusmenu` object in the process outside `/org/qindatk/GlobalMenu`,
such as the QindaQt AppShell (`composeFirstPartyMenuExport`) or QindaOffice's
exporter — keeps that export and the toolkit stands aside (`app-menu`). The
check runs when the export starts and again one and four seconds later; a
late application export makes the toolkit withdraw only its own object and
leave the window's announcement, which is the application's by then.

Keyboard shortcuts stay the application's own `Shortcut` items, as before: the
exported `shortcut` is display text. `QT_LOGGING_RULES="qindatk.globalmenu.debug=true"`
traces status and hosting changes.
