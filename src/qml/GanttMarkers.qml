// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Internal part of GanttView (D-303): vertical marker lines over the rows
// (a host's today, status date or deadline). `view.markers` is
// [{utc, label, role}]; role names a Tk.Theme.color role (default accent).
// Labels are drawn by GanttRuler in the header.
Item {
    id: layer
    required property Tk.GanttView view
    clip: true

    Repeater {
        model: layer.view.markers || []
        delegate: Rectangle {
            id: line
            required property var modelData
            required property int index
            readonly property real at: { void layer.view.calendarRevision; return layer.view.xFor(String(line.modelData.utc || "")) }
            objectName: "ganttMarker_" + index
            visible: String(line.modelData.utc || "").length > 0 && at >= 0 && at <= layer.width
            x: at - width / 2
            width: Tk.Theme.size.focusRing
            height: layer.height
            color: Tk.Theme.color[String(line.modelData.role || "accent")] || Tk.Theme.color.accent
            opacity: Tk.Theme.opacity.muted
            Accessible.role: Accessible.Indicator
            Accessible.name: String(line.modelData.label || "")
        }
    }
}
