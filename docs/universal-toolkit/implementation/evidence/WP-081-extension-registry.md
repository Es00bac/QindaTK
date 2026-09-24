<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-081: local extension registry slice

This lane implements PLT-12 as a trusted, explicit local Qt-plugin boundary.
It does not implement the rest of WP-081 (ScriptConsole, CodeEditor or
TerminalView), and does not claim support for arbitrary installed plugins.

## Code delivered

- `Tk.ExtensionRegistry` reads versioned JSON descriptors without loading
  code, diagnoses malformed/incompatible/missing/dependent providers, and
  loads a selected native plugin only after an explicit call.
- `ExtensionProvider` ABI v1 exposes `start`, declared operation `invoke`,
  `saveState` and `stop`. Runtime extension ID must match the descriptor.
- Loaded providers publish declared operations to `CapabilityRegistry` and
  retract them on unload. Unload refuses when a loaded extension depends on
  the target provider.
- Snapshot/restore retains an extension ID, descriptor path and opaque state
  when the descriptor or library is absent, so a later reinstall can recover.
- The local counter plugin fixture exercises actual Qt loader lifecycle.

## Verification

Focused test: `tst_extension_registry` passed 1/1 on the isolated Qt 6.11
build. It covers load/use/unload, capability publication, state recovery,
incompatible ABI and missing dependency. The target built with the configured
`-j24 -l24` limits.

## Deliberate boundary

Native plugin code runs in-process with the application's permissions. Plugin
binary compatibility beyond this exact host ABI is not claimed. Application
document payloads, fallback presentation and plug-in distribution remain with
the host application; this registry does not mutate them.
