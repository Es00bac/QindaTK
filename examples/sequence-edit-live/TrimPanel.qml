// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The trim row: pick a V1 cut, nudge the delta by whole frames, and let the
// SequenceEditor preview and commit the roll, ripple, slip or slide. The cut
// monitor beside it plays the same timeline at the selected cut, and the
// TransitionEditor's Preview loops the transition window there.
Tk.Flex {
    id: panel
    required property var live
    readonly property var cutList: panel.live.ready ? panel.live.cuts() : []
    readonly property int selectedCut: {
        const edit = panel.live.editor.trimEdit
        if (!edit) return -1
        for (let i = 0; i < panel.cutList.length; ++i)
            if (panel.cutList[i].outgoing === edit.leftClipId && panel.cutList[i].incoming === edit.rightClipId)
                return i
        return -1
    }

    // Adds whole frames to the delta and previews it at once.
    function nudge(framesDelta) {
        const next = Math.round(Number(trim.deltaTicks || "0") + framesDelta * panel.live.frame)
        trim.deltaTicks = String(next)
        trim.requestPreview()
    }

    gap: Tk.Theme.space.md

    Tk.Panel {
        objectName: "liveTrimPanel"
        title: qsTr("Trim · %1").arg(panel.selectedCut >= 0 ? panel.cutList[panel.selectedCut].label : qsTr("no cut"))
        iconName: "scissors"
        padding: Tk.Theme.space.xs
        height: parent.height
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex {
            width: parent.width
            height: parent.height
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex {
                width: parent.width
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.Caption { text: qsTr("Cut"); color: Tk.Theme.color.textMuted }
                Tk.Segmented {
                    objectName: "liveCutSelector"
                    small: true
                    model: panel.cutList.map(function(cut, i) {
                        return {text: panel.live.timeline.formatTicks(cut.ticks, "timecode"), value: i,
                                tooltip: cut.label}
                    })
                    currentIndex: panel.selectedCut
                    tooltip: qsTr("Choose the V1 edit point to trim")
                    onActivated: function(index) { panel.live.selectCut(index) }
                }
                Tk.Spacer { }
                Tk.Caption { text: qsTr("Nudge"); color: Tk.Theme.color.textMuted }
                Tk.Button { small: true; text: qsTr("−5F"); tooltip: qsTr("Preview five frames earlier"); onClicked: panel.nudge(-5) }
                Tk.Button { small: true; text: qsTr("−1F"); tooltip: qsTr("Preview one frame earlier"); onClicked: panel.nudge(-1) }
                Tk.Button { small: true; text: qsTr("+1F"); tooltip: qsTr("Preview one frame later"); onClicked: panel.nudge(1) }
                Tk.Button { small: true; text: qsTr("+5F"); tooltip: qsTr("Preview five frames later"); onClicked: panel.nudge(5) }
            }
            Tk.TrimView {
                id: trim
                objectName: "liveTrimView"
                width: parent.width
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                editor: panel.live.editor
                assetProvider: panel.live.assets
                viewId: "sequence-edit-live-trim"
                mode: "roll"
            }
        }
    }

    Tk.Panel {
        objectName: "liveCutMonitorPanel"
        title: qsTr("Cut monitor")
        iconName: "monitor-play"
        padding: 0
        width: Tk.Theme.size.thumbnail * 5
        height: parent.height
        Tk.SourceProgramMonitor {
            objectName: "liveCutMonitor"
            width: parent.width
            height: parent.height
            role: "program"
            viewId: "sequence-edit-live-cut"
            player: panel.live.cutPlayer
            tooltip: qsTr("The sequence at the selected cut; transition previews loop here")
        }
    }
}
