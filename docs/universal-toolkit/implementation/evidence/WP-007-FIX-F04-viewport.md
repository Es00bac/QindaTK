<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Viewport keyboard and accessible path

The document `Viewport` previously exposed Ctrl+wheel zoom and middle-drag
pan without a Tab stop, keyboard pan, or accessible action on the viewport
itself. It now exposes a named, focusable Pane with a themed focus ring.
Arrows, PageUp/PageDown and Home/End pan to hard bounds; Plus/Equal and Minus
zoom. Accessible increase/decrease call the same bounded zoom operation.
When panning cannot move at an edge, the key remains unaccepted so an
ancestor `Tk.Scroll` may handle it. A focused document child keeps its own
keyboard input. `enabled: false` or `interactive: false` rejects these user
actions; owner assignment to `zoom` is silent, while accepted zoom operations
emit `zoomModified`.

`ViewportAccessibility` checks focus/name/ring, pan and zoom keys, accessible
zoom, silent owner assignment, disabled interaction, and document-child focus.
The focused Qt 6.11.1 suite passed 5/5. The opened Compact preview shows a
cyan focus outline around the unchanged 320×220 viewport; the dump reports
`Viewport#reviewViewport 0,0 320x220`, `Rectangle#viewportFocusRing
0,0 320x220`, `Scroll#viewportScroll 0,0 320x220` and a 500×420 content
surface. Ignored artifacts: `/tmp/qindatk-viewport-focus.{qml,png,dump}`.

Native screen-reader delivery of Pane zoom actions remains platform
qualification work. The viewport does not assign focus or shortcuts to
individual objects inside the document surface.
