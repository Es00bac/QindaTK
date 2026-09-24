<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Terminal session

`Tk.TerminalSession` is the local PTY and terminal parser behind a terminal
surface. Constructing it starts no process. The application calls
`start(program, arguments, workingDirectory)` only after the user requests a
terminal action. `program` is executed directly with `execvp`; the caller
chooses whether to start a shell. There is one process per session, and a
second `start` is rejected while it runs.

`running`, `exitCode`, `errorString` and `exited(code)` expose lifecycle.
`sendText(text)` writes UTF-8 to the PTY with a bounded 1 MiB pending input
queue. `resize(rows, columns)` changes both the PTY window size and parser
screen; accepted sizes are 2–100 rows and 2–240 columns. `stop()` sends
SIGHUP to the process group and escalates to SIGKILL after two seconds.
Destroying a running session kills and reaps the child.

`screen` is an array of rows. Each row has `text` and `spans`; each span has
`text`, `foreground`, `background`, `bold`, `italic`, `underline`, and
`reverse`. Colors are `#rrggbb` or an empty string for the terminal default.
`cursorRow`, `cursorColumn` and `cursorVisible` are zero-based parser state.
`scrollback` stores plain text lines, oldest first. It defaults to 2,000
lines; `maxScrollback` can be set from 0 to 10,000. `copyText(firstRow,
lastRow)` joins inclusive whole lines across scrollback then screen. The
host owns selection, clipboard, fonts and theming in its TerminalView.

libvterm handles UTF-8, CSI/SGR and alternate screen state. The PTY is local
to Unix/Linux; this initial adapter has no Windows ConPTY implementation.
The session does not preserve terminal output across process restarts.
Applications must treat local process execution as an explicit capability;
do not bind `start()` to QML component creation.
