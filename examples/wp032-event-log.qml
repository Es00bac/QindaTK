// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Window {
    width: 1120
    height: 640
    visible: true
    color: Tk.Theme.color.bg
    title: "QindaTK Event Log"

    property string lastAction: "Append batches from the host log provider"
    Tk.EventLogModel {
        id: events
        maxEntries: 5000
    }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Heading { text: "Event stream"; level: 2 }
        Tk.EventLogView {
            objectName: "eventLogDemo"
            Tk.Flex.grow: 1
            model: events
            tooltip: "Application event stream"
            onExportRequested: function(csv) {
                lastAction = "Host export seam received " + csv.length + " characters"
            }
            onDetailRequested: function(eventId, details) {
                lastAction = "Selected " + eventId + ": " + details
            }
        }
        Tk.Caption { text: lastAction; muted: true }
    }
    Component.onCompleted: {
        const batch = []
        for (let i = 0; i < 1000; ++i) {
            const severity = i % 29 === 0 ? "error" : i % 11 === 0 ? "warning" : "info"
            batch.push({ eventId: "demo-" + i,
                timestamp: new Date(Date.now() - (1000 - i) * 47).toISOString(),
                severity: severity, source: ["audio.engine", "waveform.worker", "project.io"][i % 3],
                message: severity === "error" ? "Audio buffer underrun" : "Processed block " + i,
                details: severity === "error" ? "Output device requested data before the next block was ready." : "Block completed." })
        }
        events.appendEvents(batch)
    }
}
