<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-081 / PLT-13 — Local script runtime slice

Implementation: `Tk.ScriptRuntime`, `Tk.ScriptConsole`, and
`examples/script-console-live/Main.qml`. The runtime executes JavaScript on a
worker with JSON-only inspection data, an explicit host command allowlist,
bounded source/output/actions/arguments, timeout and cancellation. It replays
commands through `CommandRegistry` on the owner thread. Multiple commands
require an active `UndoService` model; command handlers using that service
become one undo step and roll back on failure.

Configured direct build used the measured system `MAKEOPTS` equivalent
`-j24 -l24`; `tst_script_runtime` passed 1/1 and the dedicated QML runner
passed 3/3. The live fixture passed `qtk-preview --check`; its output was
captured and inspected in [WP-081-script-console.png](WP-081-script-console.png).
The dump showed `ScriptConsole#liveScriptConsole 12,54 876x534`,
`TextArea#scriptSource 8,25 860x348` and
`TextArea#scriptOutput 8,422 860x104`. The screenshot visibly shows the
completed runtime and actual title, revision and selection output.

Open WP-081 scope: extension load/unload and versioned capabilities, persistent
macro format, process isolation, and platform qualification. This runtime does
not bound engine heap memory or isolate hostile scripts in a process. The host
must register deliberate command handlers and provide a live document snapshot.
