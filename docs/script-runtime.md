<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local script runtime and console

`Tk.ScriptRuntime` runs local JavaScript through `QJSEngine` on a worker
thread. It never injects an application `QObject` or QML delegate. The host
provides `documentSnapshot`, a JSON-value copy of the data scripts may inspect,
and `exposedCommandIds`, an explicit subset of a `CommandRegistry`. The script
sees a frozen `app.document`, `app.commands` metadata, `app.inspect(key)`,
`app.print(value)` and `app.command(id, arguments)`.

`app.command` queues semantic actions. After evaluation succeeds, the runtime
rechecks every command against the current registry and allowlist, then invokes
the handlers on the UI thread. Script errors and timeouts invoke no command.
The last successful sequence is available as `recordedCommands`; `replay()` or
`replayRecorded()` executes that macro again. A macro has at most 64 commands;
each argument object is at most 16 KiB. Source is limited to 64 KiB; printed
output is limited to 4 KiB. `maxRuntimeMs` defaults to 2,000 and is bounded to
100–10,000. `cancel()` interrupts evaluation; `running`, `status`, `output`,
`errorString`, `lastResult` and `finished(success)` report state.

`exportRecordedMacro()` returns compact versioned JSON with `format`, `version`
and `commands`; `importRecordedMacro(json)` restores that command sequence
without executing it. Imports reject malformed or oversized data and preserve
the previous recording on failure. The host owns file storage and must still
expose the command IDs before replay; unavailable commands are rejected by
the normal replay checks.

For a sequence of two or more commands, the host must attach an `UndoService`
with an active document model. The runtime opens one semantic macro and cancels
it if a handler fails or execution is canceled. Command handlers must record
their edits in that service for rollback to cover them. Single commands retain
the handler's own transaction contract. A missing history model rejects a
multi-command script before mutation.

`Tk.ScriptConsole` binds to a runtime and exposes source editing, Run, Cancel,
Replay, status and output. Ctrl+Enter runs the source. Stable internal names:
`scriptConsole`, `scriptSource`, `scriptRun`, `scriptCancel`, `scriptReplay`,
`scriptStatus`, `scriptOutput`.

This is a cooperative local automation surface, not a security sandbox for
untrusted code. The engine has no filesystem, network or application QObject
bridge, but this slice does not enforce a memory ceiling or process isolation.
Only exposed commands run, and their behavior is the application's contract.
There is no process isolation or engine heap ceiling. Extension discovery and
explicit trusted plugin loading are described in [extension registry](extension-registry.md).
