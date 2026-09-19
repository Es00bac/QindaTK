// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: one column of a Tk.DataTable. `key` names the model role
// (QAbstractItemModel) or the object property (JS array) the cell reads, and
// is also what `sortRequested` reports -- a DataTable never sorts a model
// itself, because only the model knows whether "3.9" outranks "12" as a
// number or as text.
//
// A column is either fixed (`width`) or elastic (`flex` > 0). Elastic columns
// share whatever the fixed ones leave, never shrinking below `minWidth`.
QtObject {
    id: column

    property string key: ""
    property string title: ""
    // Fixed track width, in pixels, when flex is 0.
    property real width: 80
    property real minWidth: 24
    // Share of the leftover width; 0 keeps the column at `width`.
    property real flex: 0
    property int align: Qt.AlignLeft
    // Monospaced cells: numbers that must line up column-wise.
    property bool mono: false
    property bool muted: false
    property bool visible: true
    property bool sortable: true
    property bool resizable: true
    // Descending first is right for "who is using the most", which is what a
    // numeric column is nearly always asked.
    property bool descendingFirst: false
    property string tooltip: ""

    // Cell text: `value` is the raw model value, `row` the whole record.
    property var formatter: null
    // A Component drawn instead of the label; sees `value`, `row`, `column`.
    property Component delegate: null
    // Colours the cell text by magnitude between `rampFrom` and `rampTo`.
    // A Theme.ramp.* scale; `var` because ThemeRamp is QML_ANONYMOUS
    // and so has no name a QML property declaration can use.
    property var ramp: null
    property real rampFrom: 0
    property real rampTo: 100

    function display(value, row) {
        if (column.formatter !== null) {
            return column.formatter(value, row)
        }
        return value === undefined || value === null ? "" : String(value)
    }
}
