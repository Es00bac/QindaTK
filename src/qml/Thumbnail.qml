// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A media tile that is honest about not having a picture yet.
//
// A bin row, a filmstrip frame and a poster frame all want the same thing: an
// image that may not exist, may still be loading, may have failed, and must
// occupy its space either way so the row beneath does not jump when it
// arrives.
//
// AGENT-CONTRACT: `source` may be empty, may point at an asynchronous image
// provider, and may fail. Each of those is a state a caller can see through
// `state`, and each draws something: the kind icon while empty or loading, a
// muted alert glyph on failure. It never draws nothing, and it never resizes
// itself when the image lands.
Rectangle {
    id: root

    // The image to show. Empty means "none yet" rather than "failed".
    property url source: ""
    // Drawn while there is no picture: name a shape the user can recognise
    // ("film", "music", "image", "file-text") rather than a generic box.
    property string placeholderIcon: "image"
    // Short caption drawn over the bottom edge — a duration, a page number, a
    // frame time. Empty draws no band at all.
    property string caption: ""
    // Crop fills the tile and loses edges; fit shows the whole frame and
    // leaves bars. A bin wants crop, a filmstrip frame wants fit.
    property bool crop: true
    property bool selected: false
    property string tooltip: ""

    // empty | loading | ready | failed — readable by tests and by callers
    // that want to retry.
    //
    // AGENT-GUARD: named loadState, not state. Item already has a `state`
    // property driving States/Transitions; shadowing it compiles and then
    // silently breaks any caller that declares a State on a Thumbnail.
    readonly property string loadState: root.source.toString().length === 0
                                    ? "empty"
                                    : image.status === Image.Loading ? "loading"
                                    : image.status === Image.Ready ? "ready"
                                    : image.status === Image.Error ? "failed"
                                    : "loading"

    objectName: "thumbnail"
    implicitWidth: Tk.Theme.size.thumbnail
    implicitHeight: Tk.Theme.size.thumbnailHeight
    radius: Tk.Theme.radius.sm
    color: Tk.Theme.color.canvas
    border.width: root.selected ? Tk.Theme.size.focusRing : Tk.Theme.size.border
    border.color: root.selected ? Tk.Theme.color.accent : Tk.Theme.color.border
    clip: true

    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : root.caption

    Tk.Icon {
        objectName: "thumbnailPlaceholder"
        anchors.centerIn: parent
        // The alert glyph only for a real failure: an empty tile is not an
        // error, and marking it as one trains the user to ignore the mark.
        name: root.loadState === "failed" ? "alert-triangle" : root.placeholderIcon
        size: Tk.Theme.size.iconLg
        color: root.loadState === "failed" ? Tk.Theme.color.warning
                                       : Tk.Theme.color.textDisabled
        visible: root.loadState !== "ready"
    }

    Image {
        id: image
        objectName: "thumbnailImage"
        anchors.fill: parent
        anchors.margins: root.border.width
        source: root.source
        // Asynchronous by default: these are decoded off the render thread so
        // a bin of fifty rows does not stall while they load.
        asynchronous: true
        cache: true
        smooth: true
        fillMode: root.crop ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        visible: root.loadState === "ready"
    }

    // A caption band rather than free-floating text: over a bright frame,
    // unbanded text is unreadable exactly when the frame is interesting.
    Rectangle {
        objectName: "thumbnailCaptionBand"
        visible: root.caption.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.border.width
        height: captionText.implicitHeight + Tk.Theme.space.xs * 2
        color: Tk.Theme.color.overlay

        Tk.Caption {
            id: captionText
            objectName: "thumbnailCaption"
            anchors.fill: parent
            anchors.leftMargin: Tk.Theme.space.xs
            anchors.rightMargin: Tk.Theme.space.xs
            text: root.caption
            color: Tk.Theme.color.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Tk.ToolTip {
        visible: hover.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
    HoverHandler { id: hover }
}
