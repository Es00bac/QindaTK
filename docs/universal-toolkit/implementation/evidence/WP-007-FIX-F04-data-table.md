<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 F04 — DataTable selection and accessible rows

`DataTable` now exposes a named accessible Table and names each realized Row
from the visible column titles and formatted values. Numeric formatter
results are converted to text. The row reports selected state and offers an
accessible press action. Tab can focus the table; Up and Down initialize or
move selection, while Return and Enter activate a valid current row.

Tap, keyboard activation and accessible press share `activateRow()`. Only
those accepted user actions emit `activated`; owner assignments to
`currentIndex` and arrow navigation remain silent. Disabled tables reject
activation, navigation, sorting and right-click selection. The focused QML
test covers accessible names, selection state, user-only signals and disabled
guards. The integrated system Qt 6.11.1 `tst_qml` target passed after these
changes. The full two-version gate is tracked in
[WP-007 progress](WP-007-progress.md).

Integration also added focusable ColumnHeader actions. Space, Return, Enter
and accessible press request sort; Left/Right and accessible increase/decrease
resize fixed columns through the same `resizeColumn()` operation used by
pointer drag. Owner `columnWidths` assignment remains silent. A focused QML
test checks those signals, disabled guards, accessible identity and the
visible focus ring. The 420×160 preview was dumped, grabbed and opened:
`DataTable#reviewTable 20,20 360x120` contains
`Item#headerCell_pid 0,-1 100x24` and
`Rectangle#headerFocusRing_pid -2,-2 104x28`. The PNG shows a cyan outline
around the PID header. Ignored artifacts are
`build/wp007-table-header-review.{qml,png}`.

This is still one F04 slice, not complete native table accessibility. Virtual
rows expose only the realized viewport; native assistive bridge behavior and
large-table traversal remain to be qualified.
