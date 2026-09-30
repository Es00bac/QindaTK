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
screen; accepted sizes are 2–512 rows and 2–1024 columns. `stop()` sends
SIGHUP to the process group and escalates to SIGKILL after two seconds.
Destroying a running session kills and reaps the child.

`screen` is an array of rows. Each row has `text` and `spans`; each span has
`text`, `foreground`, `background`, `bold`, `italic`, `underline`, and
`reverse`. Colors are `#rrggbb` or an empty string for the terminal default.
`cursorRow`, `cursorColumn` and `cursorVisible` are zero-based parser state.
`scrollback` exposes plain text lines, oldest first. Internally history retains
raw cells, so `rowData(row)` preserves color, styling, cell columns and glyph
widths. `copyTextRange(firstRow, firstColumn, lastRow, lastColumn)` selects
cells, with an exclusive last column; combining and wide glyphs stay intact.
Whole-line copies trim terminal padding.  `historyColumns` lets surfaces expose old wide rows after a width change.
History pop restores rows when the
screen grows, and `scrollbackOffset` counts rows evicted from the front.
Alternate-screen programs hide primary history until they leave that screen.
`clearHistory()` clears history without sending input to the application. It defaults to 2,000
lines; `maxScrollback` can be set from 0 to 10,000. `copyText(firstRow,
lastRow)` joins inclusive whole lines across scrollback then screen. The
host owns selection, clipboard, fonts and theming in its TerminalView.

libvterm handles UTF-8, CSI/SGR and alternate screen state. The PTY is local
to Unix/Linux; this initial adapter has no Windows ConPTY implementation.
`sendKey(qtKey, qtModifiers, text)` uses libvterm's application cursor and
keypad modes; traditional Ctrl-letter/punctuation, Backspace and Return encodings remain compatible
with shell line editors. `pasteText(text)` negotiates bracketed paste, normalizes
line endings, and removes control characters other than tabs/newlines.
`sendMouse(row, column, button, pressed, qtModifiers)` follows the program's
negotiated mouse protocol (`mouseMode`); buttons 1–3 and wheel 4–5 are supported.
`setFocused(bool)` reports focus when requested by the program.
`setColors(foreground, background, palette)` sets the default colors and the
first 16 ANSI palette entries, including history that used those indices.

`title`, `alternateScreen`, `reverseScreen`, `cursorBlink`, `cursorShape` and
`cursorVisible` follow libvterm properties; `bell()` announces a terminal bell.
`foregroundProcessRunning` distinguishes jobs from the idle shell process
and `workingDirectory` follows the foreground process's Linux `/proc` entry.
Launch failures return false with `errorString`; PTY masters are close-on-exec.
Final output drains in bounded event-loop turns before `exited` is delivered.

The session does not preserve terminal output across process restarts.
Applications must treat local process execution as an explicit capability;
do not bind `start()` to QML component creation.
