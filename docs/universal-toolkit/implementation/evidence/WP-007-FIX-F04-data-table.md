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

This is one F04 slice, not complete table accessibility. Header sorting and
resizing still use pointer handlers without a documented keyboard or
accessible header action. Virtual rows expose only the realized viewport;
native assistive bridge behavior and large-table traversal remain to be
qualified.
