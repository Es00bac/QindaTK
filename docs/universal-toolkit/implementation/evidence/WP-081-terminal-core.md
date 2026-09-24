<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-081 / EXT-02 — Local terminal provider slice

**State: implementation lane, not accepted.**

`TerminalSession` is a QML-exposed local PTY provider. It starts no shell on
construction. A host action must call `start(program, arguments,
workingDirectory)`. It exposes libvterm-parsed Unicode/ANSI screen rows with
style spans, cursor state, bounded plain-text scrollback, whole-line copy,
UTF-8 input, resize, exit state, and explicit stop/cleanup. The session owns
and reaps its child process and signals its process group on cancellation.

`ctest --test-dir build/wp081-terminal -R '^tst_terminal_session$'
--output-on-failure` passed 1/1 on Qt 6.11.1 and libvterm 0.3.3. The test
started an explicit local `/bin/sh` fixture, observed the initial 5×40 PTY
size, a UTF-8 `Héllo` line in a red ANSI style span, resized to 6×50, sent
input, observed exit code 7, enforced three-line scrollback, and canceled a
long-running process. The first run found a callback lifetime crash on resize;
the callback table now lives for the entire session.

The host-facing contract is [terminal-session.md](../../../terminal-session.md).
TerminalView presentation, selection and clipboard are separate. Other WP-081
scope (extensions, scripting, code editing), Qt 6.8 and an installed-consumer
gate remain open. No ConPTY backend is provided in this Linux implementation.
