// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Presenter window content bound to a running Tk.SlideShow and, when given,
// the Tk.PresentationWindows that host it (docs/slides.md#windows). The
// audience preview is a Tk.SlideFrameView of the same frame the audience
// window paints; speaker notes are bound only here.
// Next/Previous step builds like the keys (SlideShow.next/previous), and
// the annotation tools draw on the audience output: drag with the pen or
// highlighter to add ink, press and drag with the pointer to show it.
// AGENT-CONTRACT: Tk.PresentationWindows.presenterDelegate instantiates this
// with the initial properties `show` and `windows`; keep both names.
Item {
    id: control
    property Tk.SlideShow show: null
    property Tk.PresentationWindows windows: null
    property string tooltip: qsTr("Presenter console")
    readonly property bool live: !!control.show && control.show.running

    implicitWidth: presenter.implicitWidth + Tk.Theme.space.md * 2
    implicitHeight: presenter.implicitHeight + Tk.Theme.space.md * 2
    focus: true
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("Arrow keys, Space and Page keys step through builds and slides; B blanks the audience; A, P and H pick pointer, pen and highlighter; E clears the ink")

    Keys.onPressed: function(event) {
        if (!control.show) return
        switch (event.key) {
        case Qt.Key_Right: case Qt.Key_Down: case Qt.Key_Space: case Qt.Key_PageDown:
            control.show.next(); event.accepted = true; break
        case Qt.Key_Left: case Qt.Key_Up: case Qt.Key_PageUp: case Qt.Key_Backspace:
            control.show.previous(); event.accepted = true; break
        case Qt.Key_B: case Qt.Key_Period:
            control.show.blackedOut = !control.show.blackedOut; event.accepted = true; break
        case Qt.Key_Home:
            control.show.goToIndex(0); event.accepted = true; break
        case Qt.Key_End:
            control.show.goToIndex(control.show.slideCount - 1); event.accepted = true; break
        case Qt.Key_A: control.show.annotationTool = "pointer"; event.accepted = true; break
        case Qt.Key_P: control.show.annotationTool = "pen"; event.accepted = true; break
        case Qt.Key_H: control.show.annotationTool = "highlight"; event.accepted = true; break
        case Qt.Key_E: control.show.clearAnnotations(); event.accepted = true; break
        case Qt.Key_Escape:
            if (control.windows) control.windows.close(); else control.show.stop()
            event.accepted = true; break
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.bg
        Accessible.ignored: true
    }

    Tk.PresenterView {
        id: presenter
        objectName: "presenterConsoleView"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        documentId: control.show && control.show.deck ? control.show.deck.documentId : ""
        currentSlideId: control.show ? control.show.currentSlideId : ""
        previousSlideId: control.show ? control.show.previousSlideId : ""
        nextSlideId: control.show ? control.show.nextSlideId : ""
        currentIndex: control.show ? control.show.currentIndex : 0
        slideCount: control.show ? control.show.slideCount : 0
        currentTitle: control.show ? control.show.currentTitle : ""
        nextTitle: control.show ? control.show.nextTitle : ""
        speakerNotes: control.show ? control.show.speakerNotes : ""
        elapsedText: control.show ? control.show.elapsedText : "00:00"
        timerState: control.show ? control.show.timerState : "stopped"
        blackedOut: control.show ? control.show.blackedOut : false
        audienceStatus: control.windows ? control.windows.audienceStatus : "closed"
        selectedDisplayId: control.windows ? (control.windows.audienceScreenId.length
                                              ? control.windows.audienceScreenId : control.windows.audienceScreen) : ""
        displays: control.windows ? control.windows.displays : []
        capabilities: ({ canNavigate: control.live, canControlTimer: control.live,
                         canBlank: !!control.show, canSelectDisplay: !!control.windows })
        stepsBuilds: true
        canGoNext: control.live && !control.show.ended
        canGoPrevious: control.live && (control.show.ended || control.show.buildStep > 0 || control.show.currentIndex > 0)
        annotationTool: control.show ? control.show.annotationTool : "pointer"
        tooltip: control.tooltip
        onStepRequested: function(direction) { if (direction > 0) control.show.next(); else control.show.previous() }
        onSlideNavigationRequested: function(slideId, index) { control.show.goToSlide(slideId) }
        onAnnotationRequested: function(slideId, tool) {
            if (tool === "clear") control.show.clearAnnotations()
            else control.show.annotationTool = tool
        }
        onTimerActionRequested: function(action) { control.show.timerAction(action) }
        onBlankingRequested: function(blanked) { control.show.blackedOut = blanked }
        onDisplaySelectionRequested: function(displayId) { if (control.windows) control.windows.reconnect(displayId) }
        onReconnectRequested: function(displayId) { if (control.windows) control.windows.reconnect(displayId) }

        Tk.SlideFrameView {
            id: frame
            objectName: "presenterCurrentFrame"
            anchors.fill: parent
            show: control.show
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Audience output: %1").arg(control.show ? control.show.currentTitle : "")

            // Ink and pointer input in slide coordinates (SlideShow owns the ink).
            MouseArea {
                id: inkInput
                objectName: "presenterInkInput"
                anchors.fill: parent
                enabled: control.live
                cursorShape: control.show && control.show.annotationTool !== "pointer" ? Qt.CrossCursor : Qt.PointingHandCursor
                property var points: []
                function slidePoint(mouse) { const p = frame.mapToSlide(Qt.point(mouse.x, mouse.y)); return { x: p.x, y: p.y } }
                onPressed: function(mouse) {
                    control.forceActiveFocus()
                    if (control.show.annotationTool === "pointer") {
                        const p = slidePoint(mouse); control.show.setPointer(p.x, p.y)
                    } else {
                        points = [slidePoint(mouse)]
                    }
                }
                onPositionChanged: function(mouse) {
                    if (control.show.annotationTool === "pointer") {
                        const p = slidePoint(mouse); control.show.setPointer(p.x, p.y)
                    } else if (points.length < 4096) {
                        points = points.concat([slidePoint(mouse)])
                    }
                }
                onReleased: {
                    if (control.show.annotationTool === "pointer") control.show.hidePointer()
                    else if (points.length) control.show.addAnnotationStroke(points)
                    points = []
                }
                onCanceled: { control.show.hidePointer(); points = [] }
            }
        }
    }
}
