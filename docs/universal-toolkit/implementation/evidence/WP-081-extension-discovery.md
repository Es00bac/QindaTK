<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-081 — Explicit local extension discovery

This slice adds bounded discovery to the existing trusted native extension
registry. Applications or users configure absolute local directories and call
`refreshDiscovery()`. The scan reads immediate JSON files and one level of
`manifest.json` files; it does not load a plugin. Descriptors, validation
failures, dependency diagnostics and removed descriptors are presented in
`ExtensionManager`. Loading still requires a separate action.

The registry retains a running plugin until explicit unload, and keeps saved
provider state when its descriptor disappears. A later refresh can restore the
descriptor and allow a new explicit load. The native plugin remains in-process
and has the host's permissions. No sandbox or automatic filesystem watcher is
claimed.

## Focused evidence

- The C++ registry test uses the compiled `QindaTestExtension` plugin: discovers
  its descriptor without loading, explicitly loads and invokes it, removes the
  descriptor while loaded, unloads safely, preserves state, restores the
  descriptor and reloads. It also checks invalid JSON/version and missing
  dependency diagnostics.
- The QML test checks the new directory and refresh controls and the visible
  scan diagnostic for an unavailable directory.
- The [panel capture](WP-081-extension-discovery.png) shows the directory field,
  Refresh action and preserved unavailable records.

## Remaining limits

Discovery is explicit and local, with eight roots, 512 visited entries per
root, 128 candidate descriptors and 256 KiB per descriptor. Running plugin
metadata is not hot-replaced; unload then refresh to apply a changed manifest.
There is no marketplace, package installer, process isolation or live watcher.
