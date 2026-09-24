// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The presenter UI and audience surface are distinct: notes and controls only
// appear in the presenter pane; audienceContent is a separate slot.
Item {
    id: control
    property string documentId: ""
    property string currentSlideId: ""
    property string previousSlideId: ""
    property string nextSlideId: ""
    property int currentIndex: 0
    property int slideCount: 0
    property string currentTitle: ""
    property string nextTitle: ""
    property string speakerNotes: ""
    property string elapsedText: "00:00"
    property string timerState: "stopped" // stopped, running, paused
    property string audienceStatus: "connected"
    property string selectedDisplayId: ""
    property var displays: [] // [{id,label,connected,role}]
    property bool blackedOut: false
    property var capabilities: ({ canNavigate: true, canControlTimer: true, canBlank: true, canSelectDisplay: true })
    property string tooltip: qsTr("Control a live presentation")
    default property alias audienceContent: audienceSlot.data

    signal slideNavigationRequested(string slideId, int index)
    signal timerActionRequested(string action)
    signal blankingRequested(bool blanked)
    signal displaySelectionRequested(string displayId)
    signal annotationRequested(string slideId, string tool)
    signal reconnectRequested(string displayId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 4
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("Presenter view on slide %1 of %2, audience output %3")
        .arg(currentIndex + 1).arg(slideCount).arg(audienceStatus)

    function navigate(nextIndex, id) {
        if (enabled && capabilities.canNavigate && nextIndex >= 0 && nextIndex < slideCount)
            slideNavigationRequested(id, nextIndex)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { text: qsTr("Presenter view"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Badge {
                objectName: "presenterAudienceStatus"
                text: control.audienceStatus
                variant: control.audienceStatus === "connected" ? "success" : "warning"
                tooltip: qsTr("Audience display connection status")
            }
            Tk.ComboBox {
                id: displayPicker
                objectName: "presenterDisplayPicker"
                model: control.displays
                textRole: "label"
                currentIndex: {
                    for (let i = 0; i < control.displays.length; ++i)
                        if (control.displays[i].id === control.selectedDisplayId) return i
                    return -1
                }
                tooltip: qsTr("Choose audience display by stable display ID")
                enabled: control.capabilities.canSelectDisplay
                onActivated: function(index) { control.displaySelectionRequested(control.displays[index].id) }
            }
            Tk.Button {
                objectName: "presenterReconnect"
                text: qsTr("Reconnect")
                iconName: "refresh-cw"
                tooltip: qsTr("Request audience display reconnection")
                visible: control.audienceStatus !== "connected"
                enabled: displayPicker.currentIndex >= 0
                onClicked: control.reconnectRequested(control.selectedDisplayId)
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.md
            Tk.Box {
                objectName: "presenterAudienceOutput"
                color: Tk.Theme.color.canvas
                borderColor: Tk.Theme.color.borderStrong
                radius: Tk.Theme.radius.md
                padding: Tk.Theme.space.sm
                Accessible.role: Accessible.Pane
                Accessible.name: qsTr("Audience output preview")
                Accessible.description: qsTr("This surface contains no presenter notes or controls")
                Tk.Flex.grow: 3
                Tk.Flex.basis: 0
                Item {
                    id: audienceSlot
                    anchors.fill: parent
                    visible: !control.blackedOut
                    Accessible.ignored: control.blackedOut
                }
                Tk.Flex {
                    anchors.fill: parent
                    visible: control.blackedOut
                    direction: Tk.Flex.Column
                    align: Tk.Flex.Center
                    Tk.Icon { name: "monitor-off"; size: Tk.Theme.size.iconLg; color: Tk.Theme.color.textMuted }
                    Tk.Caption { text: qsTr("Audience output is blanked") }
                }
                Rectangle {
                    anchors.fill: parent
                    color: Tk.Theme.alpha(Tk.Theme.color.focus, Tk.Theme.opacity.hidden)
                    border.color: parent.activeFocus ? Tk.Theme.color.focus
                                                       : Tk.Theme.alpha(Tk.Theme.color.focus, Tk.Theme.opacity.hidden)
                    border.width: Tk.Theme.size.focusRing
                    Accessible.ignored: true
                }
            }
            Tk.Flex {
                objectName: "presenterPrivatePanel"
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.sm
                Tk.Flex.grow: 2
                Tk.Flex.basis: 0
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.Button {
                        objectName: "presenterPrevious"
                        text: qsTr("Previous")
                        iconName: "chevron-left"
                        tooltip: qsTr("Go to previous slide")
                        enabled: control.currentIndex > 0 && control.capabilities.canNavigate
                        onClicked: control.navigate(control.currentIndex - 1, control.previousSlideId)
                    }
                    Tk.Caption { text: qsTr("%1 / %2").arg(control.currentIndex + 1).arg(control.slideCount); mono: true }
                    Tk.Button {
                        objectName: "presenterNext"
                        text: qsTr("Next")
                        iconName: "chevron-right"
                        tooltip: qsTr("Go to next slide")
                        enabled: control.currentIndex + 1 < control.slideCount && control.capabilities.canNavigate
                        onClicked: control.navigate(control.currentIndex + 1, control.nextSlideId)
                    }
                }
                Tk.Box {
                    objectName: "presenterNextSlide"
                    color: Tk.Theme.color.panelAlt
                    radius: Tk.Theme.radius.sm
                    padding: Tk.Theme.space.sm
                    implicitHeight: Tk.Theme.size.control * 2
                    Tk.Flex {
                        anchors.fill: parent
                        direction: Tk.Flex.Column
                        Tk.Caption { text: qsTr("Up next") }
                        Tk.Label { text: control.nextTitle.length ? control.nextTitle : qsTr("End of presentation"); elide: Text.ElideRight }
                    }
                }
                Tk.Box {
                    objectName: "presenterNotes"
                    color: Tk.Theme.color.panel
                    borderColor: Tk.Theme.color.border
                    radius: Tk.Theme.radius.sm
                    padding: Tk.Theme.space.sm
                    Tk.Flex {
                        anchors.fill: parent
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Overline { title: qsTr("Private speaker notes") }
                        Tk.Scroll {
                            overflowY: Tk.Scroll.Auto
                            Tk.Label { text: control.speakerNotes; wrapMode: Text.WordWrap; selectable: true }
                        }
                    }
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.Badge { objectName: "presenterTimer"; text: control.elapsedText; variant: "info"; tooltip: qsTr("Elapsed presentation time from host clock") }
                    Tk.Button {
                        objectName: "presenterTimerAction"
                        text: control.timerState === "running" ? qsTr("Pause timer") : control.timerState === "paused" ? qsTr("Resume timer") : qsTr("Start timer")
                        iconName: control.timerState === "running" ? "pause" : "play"
                        tooltip: qsTr("Control the presentation timer")
                        enabled: control.capabilities.canControlTimer
                        onClicked: control.timerActionRequested(control.timerState === "running" ? "pause" : "start")
                    }
                    Tk.Button {
                        objectName: "presenterBlank"
                        text: control.blackedOut ? qsTr("Show audience") : qsTr("Blank audience")
                        iconName: "monitor-off"
                        tooltip: qsTr("Blank or restore audience output")
                        enabled: control.capabilities.canBlank
                        onClicked: control.blankingRequested(!control.blackedOut)
                    }
                    Tk.ComboBox {
                        objectName: "presenterAnnotationTool"
                        model: [qsTr("Pointer"), qsTr("Pen"), qsTr("Highlight")]
                        tooltip: qsTr("Choose a live audience annotation tool")
                        onActivated: function(index) { control.annotationRequested(control.currentSlideId, ["pointer", "pen", "highlight"][index]) }
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                    }
                }
            }
        }
    }
}
