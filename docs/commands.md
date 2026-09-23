<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Application commands, shortcuts, and transient focus

WP-011 adds a shared semantic command layer. Menus, toolbar buttons, the
command palette and keyboard shortcuts can refer to the same stable command
ID, while the application owns the command operation and its current scope.
The current implementation is a core service with selected QML adapters; it
does not make every existing control a command presentation automatically.

## Ownership model

`CommandContext` stores live scope instances. Each scope has a stable opaque
token, a caller-provided scope ID, an optional parent token, a weak `QObject`
owner and a typed context snapshot. Destroying an owner removes its scope and
descendants. Activating a token changes the active path used for resolution;
the path is most-specific first. The first pushed scope becomes active when
there is no active scope. Removing the active subtree falls back to its
surviving parent, then to the first remaining scope.

`CommandRegistry` owns global descriptors and scope-bound handlers. A
descriptor contains `id`, `label`, `iconName`, `section`, `keywords`,
`defaultShortcut` and `checkable`. A handler has a weak owner, an operation
callback and an optional state callback. Resolution uses the first live
handler on the active context path. The optional state callback returns
`enabled`, `checked`, `visible` and `disabledReason`. A descriptor without a
handler in the active path is unavailable; the registry does not infer an
operation from its label or shortcut.

Each accepted invocation resolves state, captures a `CommandInvocationContext`
snapshot, assigns the descriptor ID to `request.operationId`, checks the
snapshot cancellation token and calls one handler. The callback returns an
`OperationError`: a default constructed value means success; a populated
error rejects the operation. The registry emits `invoked(id, scopeToken)` on
success and `rejected(id, reason)` on rejection. Its QML `trigger(id)` returns
whether the operation succeeded. `query(id)` returns a QVariant map and
`paletteCommands()` returns visible resolved rows.

The context snapshot inherits document revision, focus view, selection and
tool values from ancestors unless a more specific scope supplies them.
`updateScope(token, values)` accepts `snapshotHeader` (the
`ContractCodec::snapshotHeaderFromQml` map), `focusedViewId`, `selectionIds`
(IDs as strings) and `toolId`. The snapshot's `request.targets` follows the
resolved selection. Its cancellation token comes from the most-specific
active frame. These services are GUI-thread objects; worker code should
receive immutable contract snapshots rather than retain QObjects.

## C++ registration

Command metadata and handler callbacks are registered from C++; they are not
QML-callable registration APIs. A typical application owns the context,
registry and scope owners for the lifetime of its windows/documents:

```cpp
QindaTK::CommandContext context;
QindaTK::CommandRegistry registry;
registry.setContext(&context);

QObject appOwner;
QObject documentOwner;
const auto app = context.pushScope("application", &appOwner);
const auto document = context.pushScope("document", &documentOwner, app);
context.activate(document);

registry.registerCommand({"file.save", "Save", "save", "File",
                          "write commit", "Ctrl+S", false});
registry.registerHandler(document, "file.save", &documentOwner,
    [](const QindaTK::CommandInvocationContext &invocation,
       const QVariantMap &arguments) {
        Q_UNUSED(arguments)
        // Save invocation.request.expected.documentId at its expected revision.
        Q_UNUSED(invocation)
        return QindaTK::OperationError{};
    },
    [](const QindaTK::CommandInvocationContext &invocation) {
        const bool hasDocument = invocation.request.expected.documentId.isValid();
        return QindaTK::CommandState{hasDocument, false, true,
                                     hasDocument ? QString{} : QStringLiteral("No document")};
    });
```

The example's operation body is a placeholder for the host's real save path.
Keep handler owners alive for as long as their operations are valid. Replacing
a handler removes the old registration; destroying either the handler owner
or its scope owner makes it unavailable. A registry context replacement
clears scope handlers because their tokens belong to the old tree. Call
`invalidate(id)` after external data used by a state callback changes so
bindings refresh.

## QML presentations

Expose the application-owned services to the QML root (for example as
registered singleton instances or root properties), then use the same
registry/map in each presentation. Exact injection setup depends on the
application's engine architecture.

```qml
import QtQuick
import QindaTK as Tk

Tk.ToolBar {
    Tk.CommandButton {
        registry: app.commands
        shortcutMap: app.shortcuts
        commandId: "file.save"
        iconOnly: true
        onTriggered: app.announceSaved()
    }
}

Tk.MenuItem {
    text: "Save"             // replaced by the command label while bound
    commandRegistry: app.commands
    shortcutMap: app.shortcuts
    commandId: "file.save"
}

Tk.CommandPalette {
    registry: app.commands
    shortcutMap: app.shortcuts
}
```

`CommandBinding` is the nonvisual QML projection used by `CommandButton` and
`MenuItem`. Its `label`, `iconName`, `shortcut`, `enabled`, `checked`,
`checkable` and `reason` refresh when the registry changes. `trigger()` uses
the registry's one guarded invocation path. `CommandButton.triggered` and
`CommandPalette.activated(id)` are presentation-level notifications after
successful registry dispatch. They are not substitutes for the C++ handler.

`MenuItem` keeps its direct `text`, `shortcut`, `iconName`, check state and
`onTriggered` behavior when no command is bound. With a registry and nonempty
`commandId`, the binding synchronizes those presentation fields and its own
`onTriggered` calls `CommandBinding.trigger()`. Avoid adding a second
operation in the same item's `onTriggered`; observe the registry's
`invoked` signal or use `onTriggered` only for UI feedback.

`CommandButton` is a toolbar-friendly wrapper around `Tk.Button`. It mirrors
the command label, icon, enabled/checkable/checked state, and disabled reason.
The contained button is exposed as read-only `button` for customization. Its
`tooltip` is used for icon-only naming and otherwise falls back to the
disabled reason or command label.

`CommandPalette` retains caller-fed `commands` mode for compatibility. If
`registry` is assigned, the registry becomes the row source; only visible
commands are listed, disabled rows stay visible with their reason, and a
failed trigger leaves the palette open. Without a registry, accepted palette
selection only emits `activated(id)` for the application to handle.

## ShortcutMap

`ShortcutMap` is a window-local event filter. Set its `registry` and
`window`, then bind portable `QKeySequence` strings. Bindings may be global
(empty scope token) or attached to a live context token. The nearest active
scope binding takes priority over ancestor and global bindings. Scoped
bindings are removed when their scope closes or the registry context changes.
A default
sequence can come from the descriptor with `bindCommand`; `bind` accepts an
explicit portable sequence.

```qml
Tk.ShortcutMap {
    id: shortcuts
    registry: app.commands
    window: app.window
    Component.onCompleted: {
        bindCommand("file.save")
        bind("edit.comment", "Ctrl+K, Ctrl+C", app.documentScope)
    }
}
```

`setOverride(id, sequence)`, `resetOverride(id)`, `overrides()` and
`loadOverrides(map)` provide per-command portable-text persistence. Overrides
are held by this object; the host chooses where and when to save them.
`displaySequence(id)` returns native-formatted text for the most-specific
active binding; `hasBinding(id)` distinguishes an unbound command from an
explicitly cleared override. `conflicts()` reports overlapping
bindings at the same active-scope rank. Dispatch emits `conflict(sequence,
commandIds)` and invokes neither command for an ambiguous exact match or an
exact-versus-prefix collision. Chords wait for `chordTimeoutMs` (1200 ms by
default); Escape, focus change, registry/window replacement and binding
changes cancel a pending chord. A mismatch cancels the pending chord and
retries the new stroke once as a fresh sequence.

The event filter ignores key auto-repeat. During IME composition it cancels
the chord and lets the event continue. For a focused item accepting input
methods, unmodified/Shift typing and standard text-editing keys are left to
the editor unless a binding opts into `allowInTextInput`. Other modified
shortcuts can still dispatch from a text input. A host using a custom text
editor must expose Qt's input-method focus and composition state correctly.
The public `dispatchPortable(stroke, textInput, composing)` helper follows the
same routing rules and is useful to drive integrations.

Shortcut registration does not install native menus or persist overrides by
itself. A caller must set the target window, bind commands, handle reported
conflicts and store overrides if desired. It also does not coordinate with
operating-system or other application-level shortcut systems.

## FocusManager

`FocusManager` tracks transient surfaces per window. `enterSurface(owner,
target, window, parentToken)` returns an opaque token and records the current
focused item through weak pointers. `activate(token)` marks the surface active
and focuses a visible, enabled target. Nested tokens close with their parent.
`deactivate(token)` removes the active status and attempts safe focus restore;
`closeSurface(token)` removes the surface and descendants.

Focus restores only while focus is still in the closing target tree or Qt has
cleared focus. A later explicit focus move therefore wins. The saved item must
still exist, be visible and enabled, and belong to the same window. Destroyed
owners or targets are not revived; saved references are weak. The manager
does not create popups, trap modal focus, or choose a logical editor target
for the application. Create one manager per window and call it as part of the
host's popup lifecycle.

## Current integration boundary

The shared registry is integrated with `MenuItem`, `CommandButton` and
`CommandPalette`. `ToolBar` remains a layout container: place command buttons
inside it. Existing `Button`, direct `MenuItem` and caller-fed palette
signals continue to work without a registry. There is not yet an automatic
adapter for every control, a preferences UI, native shortcut export, command
undo/redo policy, asynchronous handler protocol, or a global focus policy.
The state callback is expected to be a query; applications should call
`invalidate()` after its inputs change rather than mutating command structures
during resolution.

See [application contracts](application-contracts.md) for the immutable
invocation snapshot and cancellation types.
