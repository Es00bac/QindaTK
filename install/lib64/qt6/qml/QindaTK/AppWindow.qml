// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the application window of a
// document app. Four bands, top to bottom: the `menuBar` (a Tk.MenuBar),
// the `toolBars` (any items, usually Tk.ToolBar, stacked in a column),
// the content (the default property), then the optional `findBar` and the
// `statusBar` at the very bottom. Every band is as tall as its content
// and the content band takes the rest. The background is the theme's bg
// role, and the window palette mirrors the theme so native popups (file
// dialogs off-portal, tooltips of foreign controls) inherit it.
//
// Sizing: set width/height once in the component. Never resize a window
// after it is shown from inside the app (the QindaQt desktop's window
// containers own grouped geometry, ADR-0117).
T.ApplicationWindow {
    id: window

    default property alias content: contentHost.data
    property alias toolBars: toolBarColumn.data
    property alias findBar: findHost.data
    property alias statusBar: statusHost.data
    readonly property Item contentHostItem: contentHost
    // View ▸ Show Toolbars / Show Status Bar: hide the whole band.
    property bool toolBarsVisible: true
    property bool statusBarVisible: true

    visible: true
    width: 960
    height: 680
    minimumWidth: 420
    minimumHeight: 320
    color: Tk.Theme.color.bg

    // The complete semantic palette, so anything native (QtQuick.Dialogs
    // fallbacks, foreign controls) reads the theme instead of the host's.
    palette.window: Tk.Theme.color.bg
    palette.base: Tk.Theme.color.inputBg
    palette.alternateBase: Tk.Theme.color.panelAlt
    palette.button: Tk.Theme.color.controlBg
    palette.text: Tk.Theme.color.text
    palette.windowText: Tk.Theme.color.text
    palette.buttonText: Tk.Theme.color.text
    palette.placeholderText: Tk.Theme.color.textMuted
    palette.toolTipBase: Tk.Theme.color.tooltipBg
    palette.toolTipText: Tk.Theme.color.tooltipText
    palette.highlight: Tk.Theme.color.accent
    palette.highlightedText: Tk.Theme.color.accentContrast
    palette.light: Tk.Theme.color.borderStrong
    palette.mid: Tk.Theme.color.divider
    palette.dark: Tk.Theme.color.borderStrong
    font.family: Tk.Theme.font.family
    font.pixelSize: Tk.Theme.font.body

    // The template places menuBar, header, contentItem and footer in a
    // column itself; toolbars ride in the header, find bar and status bar
    // in the footer. AGENT-GUARD: the header and footer are plain Items
    // that report the column's implicit height (D-005): the window sets
    // the header's size during its own relayout, and a Flex sitting there
    // directly re-polished itself from inside that relayout without end.
    header: Item {
        objectName: "appHeader"
        implicitHeight: toolBarColumn.visible ? toolBarColumn.implicitHeight : 0
        Tk.Flex {
            id: toolBarColumn
            objectName: "appToolBars"
            anchors.left: parent.left
            anchors.right: parent.right
            direction: Tk.Flex.Column
            visible: window.toolBarsVisible && children.length > 0
        }
    }

    footer: Item {
        objectName: "appFooter"
        implicitHeight: (findHost.visible ? findHost.implicitHeight : 0)
                        + (statusHost.visible ? statusHost.implicitHeight : 0)
        Tk.Flex {
            id: findHost
            objectName: "appFindBar"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            direction: Tk.Flex.Column
            visible: children.length > 0
        }
        Tk.Flex {
            id: statusHost
            objectName: "appStatusBar"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            direction: Tk.Flex.Column
            visible: window.statusBarVisible && children.length > 0
        }
    }

    background: Rectangle {
        color: Tk.Theme.color.bg
    }

    Item {
        id: contentHost
        objectName: "appContent"
        anchors.fill: parent
        readonly property Item single: children.length === 1 ? children[0] : null
        // Same seating rule as Tk.Box: one unanchored child fills the band.
        onChildrenChanged: Qt.callLater(window.seatContent)
    }

    function seatContent() {
        const single = contentHost.single
        if (single === null || Tk.LayoutInfo.hasAnchors(single)) {
            return
        }
        if (!Tk.LayoutInfo.hasExplicitWidth(single) && !Tk.LayoutInfo.hasExplicitHeight(single)) {
            single.anchors.fill = contentHost
        }
    }
    Component.onCompleted: seatContent()
}
