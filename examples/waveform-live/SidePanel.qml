// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Session side of examples/waveform-live: the engine on the null device and
// its measured output, the source's edit list with the model's own undo and
// redo, and the state of both worker-thread analyses.
Tk.Panel {
    id: panel
    required property var page
    required property var session
    required property var engine
    required property var peaks
    required property var spectrum
    readonly property var stats: panel.engine.statistics

    title: qsTr("Session")
    iconName: "sliders-horizontal"
    collapsible: false
    padding: Tk.Theme.space.md

    function kib(bytes) { return (Number(bytes || 0) / 1024).toFixed(0) + " KiB" }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Overline { title: qsTr("Engine · built-in null device") }
        Tk.KeyValue {
            objectName: "liveEngineState"
            width: parent.width
            key: qsTr("State")
            value: qsTr("%1 · %2 Hz · %3 frames").arg(panel.engine.state).arg(panel.engine.sampleRate).arg(panel.engine.bufferSize)
        }
        Tk.KeyValue {
            objectName: "liveEngineCallbacks"
            width: parent.width
            key: qsTr("Callbacks")
            value: qsTr("%1 · xruns %2").arg(panel.stats.callbacks || 0).arg(panel.stats.xruns || 0)
        }
        Tk.KeyValue {
            objectName: "livePosition"
            width: parent.width
            key: qsTr("Playing")
            value: panel.engine.transport.playing
                   ? qsTr("%1 (loop)").arg(panel.page.seconds(panel.engine.transport.positionSample))
                   : qsTr("stopped")
        }
        Tk.AudioMeter {
            objectName: "liveOutputMeter"
            width: parent.width
            meter: panel.engine.outputMeter
            tooltip: qsTr("Engine output: sample peak and RMS of what the device is sent")
        }

        Tk.Overline { title: qsTr("Edit list · revision %1").arg(panel.page.revision) }
        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "liveUndo"
                small: true
                iconName: "undo-2"
                text: qsTr("Undo")
                Tk.Flex.flex: 1
                enabled: panel.session.edits.canUndo
                tooltip: panel.session.edits.canUndo ? qsTr("Undo %1").arg(panel.session.edits.undoText) : qsTr("Nothing to undo")
                onClicked: panel.session.edits.undo()
            }
            Tk.Button {
                objectName: "liveRedo"
                small: true
                iconName: "redo-2"
                text: qsTr("Redo")
                Tk.Flex.flex: 1
                enabled: panel.session.edits.canRedo
                tooltip: panel.session.edits.canRedo ? qsTr("Redo %1").arg(panel.session.edits.redoText) : qsTr("Nothing to redo")
                onClicked: panel.session.edits.redo()
            }
        }
        Tk.Scroll {
            width: parent.width
            height: parent.height
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex {
                id: editRows
                width: parent.width
                direction: Tk.Flex.Column
                Repeater {
                    model: panel.page.editList
                    delegate: Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "liveEdit" + index
                        width: editRows.width
                        small: true
                        text: qsTr("%1. %2").arg(index + 1).arg(panel.page.describe(modelData))
                        tooltip: text
                    }
                }
                Tk.Label {
                    visible: panel.page.editList.length === 0
                    text: qsTr("No edits: the original recording")
                    muted: true
                }
            }
        }
        Tk.Caption {
            width: parent.width
            text: panel.session.edits.canRedo ? qsTr("Next redo: %1").arg(panel.session.edits.redoText)
                                              : qsTr("%1 undo steps held").arg(panel.session.edits.historyCount)
            color: Tk.Theme.color.textMuted
            elide: Text.ElideRight
        }

        Tk.Overline { title: qsTr("Analysis · worker threads") }
        Tk.KeyValue {
            objectName: "livePeaksState"
            width: parent.width
            key: qsTr("Peaks")
            value: qsTr("%1 · %2 columns").arg(panel.peaks.state).arg(panel.peaks.columns.length)
        }
        Tk.KeyValue {
            objectName: "liveSpectrumState"
            width: parent.width
            key: qsTr("Spectrum")
            value: qsTr("%1 · %2 cells").arg(panel.spectrum.state).arg(panel.spectrum.tiles.length)
        }
        Tk.KeyValue {
            width: parent.width
            key: qsTr("Tile cache")
            value: qsTr("%1 tiles · %2").arg(panel.spectrum.cachedTiles).arg(panel.kib(panel.spectrum.cacheBytes))
        }
        Tk.KeyValue {
            width: parent.width
            key: qsTr("Spectral patches")
            value: panel.kib(panel.session.edits.patchBytes)
        }
    }
}
