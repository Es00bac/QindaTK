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
Destroying a running session hangs up first: it sends SIGHUP to the process
group, closes the PTY, and waits up to 500 ms for the program to exit, so the
program can save its state and stop its own helpers. A program that ignores
the hangup is then killed, and the child is always reaped.

`screen` is an array of rows. Each row has `text` and `spans`; each span has
`text`, `foreground`, `background`, `bold`, `dim`, `italic`, `underline`, and
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

## Protocols beyond libvterm

libvterm 0.3 ignores most of what current terminal programs negotiate, and has
no hook for private modes, graphic attributes or mode requests. The session
therefore tokenizes the child's output before libvterm sees it, answers the
sequences below itself, and passes everything else through unchanged
([D-363](decisions.md)). Replies are written in stream order, so a probe that
ends with a device-attributes request reads its answers before that reply.

| Program sends | Session does |
| --- | --- |
| `CSI ? 2026 h` … `CSI ? 2026 l` (synchronized output) | Holds the frame and applies it whole. A frame is released after 1 s, at 8 MiB, on resize and at process exit, so a program that dies inside one cannot freeze the screen. `CSI ? 2026 $ p` reports the mode. |
| `CSI > flags u`, `CSI < n u`, `CSI = flags ; mode u`, `CSI ? u` (kitty keyboard) | Keeps a flag stack per screen, exposed as `keyboardFlags`, and encodes keys accordingly. |
| `CSI > 4 ; level m`, `CSI ? 4 m` (xterm modifyOtherKeys) | Encodes otherwise ambiguous keys as `CSI 27 ; modifiers ; code ~` while no kitty flags are set. |
| `SGR 2` / `22` (faint) | Rows report `dim`. Underline colour (`58`, `59`), overline and font selection are removed rather than misread. |
| `CSI ? 2004 h/l` | Still handled by libvterm; `bracketedPaste` reports it to the host. |
| `CSI ? 1007 h/l` (alternate scroll) | `alternateScroll`, on by default: the host may turn the wheel into arrow keys on the alternate screen. |
| `OSC 10/11/12 ; ?`, `OSC 4 ; n ; ?` | Replies with the colours given to `setColors()`. `OSC 12 ; colour` sets `cursorColor`; `OSC 112` clears it. |
| `CSI ? 996 n`, `CSI ? 2031 h/l` | Reports dark or light from the background, and announces a change when `setColors()` flips it. |
| `CSI > q` (XTVERSION) | Replies with the application name and version. |
| `DCS + q … ST` (XTGETTCAP) | Answers `TN`, `Co`, `RGB` and `Ms`; other names are reported unknown. |
| `CSI 14 t`, `CSI 16 t`, `CSI 18 t` | Reports text-area and cell size once the host has called `setCellSize()`. |
| `OSC 52 ; targets ; base64` | Emits `clipboardWriteRequested(text, primarySelection)`. A read request gets an empty reply: programs can never read the clipboard. |
| `OSC 9 ; text`, `OSC 777 ; notify ; title ; body`, `OSC 99` | Emits `notificationRequested(title, body)`. |
| `OSC 7501` (program status), `OSC 9 ; 4`, `OSC 133 ; A` | Maintains status records; see below. |

`sendKeyEvent(key, modifiers, text, eventType)` is `sendKey()` for hosts that
also see repeats (2) and releases (3). With kitty flags set, modified keys are
sent as `CSI number ; modifiers u`, so Shift+Enter, Ctrl+Enter, Ctrl+I and Tab,
or Escape and Alt+key, are distinct. Enter, Tab and Backspace keep their
single bytes unless a program asks for every key as an escape code, and a
release is reported only to a program that asked for event types. A host may
keep sending typed text with `sendText()`, which is what input methods need,
unless `keyboardFlags & 8` asks for every key as an escape code. With nothing negotiated, keys use their
traditional bytes: Ctrl+letter is always the C0 control, including Ctrl+I,
Ctrl+J and Ctrl+M.

Every clipboard, notification and status request comes from the program in the
terminal and is untrusted. The session bounds sizes, removes control and
direction-override characters from text shown outside the grid, and never
echoes a record back. The host decides whether to honour a request.

### Program status (OSC 7501)

`programStatus` is the record a single indicator should show, or an empty map:
`state` (`idle`, `working`, `done`, `blocked`, `error`), `kind` (`permission`,
`question`, `auth`; only when blocked), `app`, `title`, `message`, `progress`
(0–100, or -1) and `records`. `programStatusRecords()` lists every record.
A blocked record outranks error, working, done and idle. The session follows
revision 0.4 of the protocol: a report replaces its record; `state=clear`
removes a record and those beneath it; an unknown state, a malformed id, text
that does not decode, or a broken limit (4096-byte sequence, 16-byte key,
32-byte app, 128-byte id, 2048-byte message, 192-byte title, 256 records)
discards the whole report. Process exit and a new shell prompt (`OSC 133;A`)
end working and blocked records; done and error remain until the host calls
`acknowledgeProgramStatus()`, normally when the user returns to the terminal.
A full reset clears everything. `OSC 9;4` progress is shown as the root record
until the program sends a real status report.

The child process starts with `TERM=xterm-256color`, `COLORTERM=truecolor` and
`TERM_PROGRAM`/`TERM_PROGRAM_VERSION` naming the host application. Variables
that identify another terminal or multiplexer are removed, so a program does
not address this terminal in the dialect of the one that launched the host.

`screenChanged` is emitted after a 2 ms quiet gap, or 8 ms after the first
unannounced byte, rather than once per read. Reads are never stale: `screen`,
`rowData()`, `copyText()` and `scrollback` always reflect everything parsed so
far. A view that keeps its own model is current as of the last signal.
`exited` is still delivered after the final screen.

The session does not preserve terminal output across process restarts.
Applications must treat local process execution as an explicit capability;
do not bind `start()` to QML component creation.
