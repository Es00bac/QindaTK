// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: a modal dialog on the popover surface behind a dim
// scrim: overline title (+ caption subtitle), the dialog's content as
// its default property, and a footer with a secondary and a primary
// button (`primaryText` / `secondaryText`; `destructive` paints the
// primary in the danger variant). Escape rejects, Enter accepts.
// `standardButtons` from T.Dialog is deliberately unused: the footer is
// ours, so set `footer:` to replace it wholesale instead. `tertiaryText`
// adds a third, left-aligned button (Save / Discard / Cancel dialogs);
// `primaryEnabled: false` greys the primary and blocks Enter.
T.Dialog {
    id: dialog

    default property alias content: body.content
    property string subtitle: ""
    property string primaryText: qsTr("OK")
    property string secondaryText: qsTr("Cancel")
    property bool destructive: false
    property real dialogWidth: 420
    // A third, left-aligned button ("Discard", "Don't save"): emits tertiary().
    property string tertiaryText: ""
    // Gate for the primary action (a prompt that must not accept "").
    property bool primaryEnabled: true

    signal tertiary()

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: parent && parent.width > 0 ? Math.min(dialog.dialogWidth, parent.width - Tk.Theme.space.xl * 2)
                                      : dialog.dialogWidth
    readonly property real naturalHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    // AGENT-GUARD: The body may exceed the host window. Keep actions inside
    // the window and let the body scroll instead of sizing the popup offscreen.
    implicitHeight: parent && parent.height > 0
        ? Math.min(naturalHeight, Math.max(0, parent.height - Tk.Theme.space.xl * 2))
        : naturalHeight
    modal: true
    dim: true
    focus: true
    padding: 0
    spacing: 0
    closePolicy: T.Popup.CloseOnEscape

    onOpened: {
        if (primaryButton.visible) {
            primaryButton.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    header: Tk.Box {
        objectName: "dialogHeader"
        visible: dialog.title.length > 0 || dialog.subtitle.length > 0
        padding: Tk.Theme.space.md
        paddingBottom: Tk.Theme.space.sm
        borderBottom: 1
        borderColor: Tk.Theme.color.divider

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Overline {
                objectName: "dialogTitle"
                title: dialog.title
                wide: true
                color: Tk.Theme.color.text
            }
            Tk.Caption {
                visible: dialog.subtitle.length > 0
                text: dialog.subtitle
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
    }

    // AGENT-NOTE: a Popup is not an Item, so Keys and Accessible attach to
    // the body viewport; `focus: true` above hands it the focus on open.
    contentItem: Tk.Scroll {
        id: body
        objectName: "dialogBody"
        padding: Tk.Theme.space.md
        Accessible.role: Accessible.Dialog
        Accessible.name: dialog.title
        Keys.onReturnPressed: if (dialog.primaryEnabled) dialog.accept()
        Keys.onEnterPressed: if (dialog.primaryEnabled) dialog.accept()

        Connections {
            target: body.Window.window
            function onActiveFocusItemChanged() {
                const item = target.activeFocusItem
                let ancestor = item
                while (ancestor && ancestor !== body.contentItem)
                    ancestor = ancestor.parent
                if (ancestor)
                    body.ensureVisible(item)
            }
        }
    }

    footer: Tk.Box {
        objectName: "dialogFooter"
        padding: Tk.Theme.space.md
        paddingTop: Tk.Theme.space.sm
        borderTop: 1
        borderColor: Tk.Theme.color.divider

        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.End
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm

            Tk.Button {
                objectName: "dialogTertiary"
                visible: dialog.tertiaryText.length > 0
                text: dialog.tertiaryText
                variant: "ghost"
                onClicked: dialog.tertiary()
            }
            Tk.Spacer { visible: dialog.tertiaryText.length > 0 }
            Tk.Button {
                objectName: "dialogSecondary"
                visible: dialog.secondaryText.length > 0
                text: dialog.secondaryText
                variant: "default"
                onClicked: dialog.reject()
            }
            Tk.Button {
                id: primaryButton
                objectName: "dialogPrimary"
                visible: dialog.primaryText.length > 0
                text: dialog.primaryText
                variant: dialog.destructive ? "danger" : "accent"
                available: dialog.primaryEnabled
                onClicked: dialog.accept()
            }
        }
    }

    background: Rectangle {
        color: Tk.Theme.color.popoverBg
        border.width: 1
        border.color: Tk.Theme.color.popoverBorder
        radius: Tk.Theme.radius.lg
    }

    T.Overlay.modal: Rectangle {
        color: Tk.Theme.color.overlay
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Tk.Theme.motion.fast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Tk.Theme.motion.fast }
    }
}
