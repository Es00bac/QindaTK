<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Service decisions

Continuation of [decisions.md](decisions.md).

## D-030 — Independent settings and workspace files

**Context.** A creative application must restart with preferences, windows,
views and panels without treating those records as document edits. **Decision.**
Typed preferences and workspace topology use separate versioned JSON schemas
and atomic files. Document bytes remain under `DocumentSession`; panel
placement remains under `DockModel`. Unknown setting keys and workspace
record fields are preserved. Restored windows are constrained to available
screens, and missing document URIs remain visible as unavailable references.
Floating shade is transient host presentation state and resets on workspace
switch. **Consequences.** Apps can replace storage independently, retain
plugin settings and unresolved panel references, and restore multi-view
sessions without mutating documents. Native window creation is an application
or later `WindowHost` task.
