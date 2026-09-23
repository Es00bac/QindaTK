// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    objectName: "installedTimelineRoot"
    width: 960
    height: 540

    Tk.TimelineModel {
        id: timelineModel
        objectName: "installedTimelineModel"
    }

    Tk.EditTimeline {
        id: timeline
        objectName: "installedEditTimeline"
        anchors.fill: parent
        model: timelineModel
        snapEnabled: false
        displayMode: "frames"
    }
}
