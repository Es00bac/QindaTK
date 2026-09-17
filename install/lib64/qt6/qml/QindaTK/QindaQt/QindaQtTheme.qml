// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaQt.Tokens 1.0
import QindaTK as Tk

// AGENT-CONTRACT: feeds the QindaQt desktop's QST-1 semantic tokens into
// QindaTK's Theme. Instantiate one in the application root:
//
//     import QindaTK.QindaQt
//     QindaQtTheme { }
//
// Every republish of the Tokens singleton (theme switch, accessibility
// change) re-derives QindaTK's colour roles, so both the desktop's own
// controls and QindaTK's read the same palette. When the Tokens singleton
// reports not ready, the toolkit keeps its current preset instead of
// painting invented colours on top of an unpublished theme.
QtObject {
    id: bridge

    property bool active: true
    readonly property bool applied: bridge._applied
    property bool _applied: false

    function sync() {
        if (!bridge.active || !Tokens.ready) {
            return
        }
        bridge._applied = Tk.Theme.applyQst({
            "bg": Tokens.bg,
            "fg": Tokens.fg,
            "accent": Tokens.accent,
            "state": Tokens.state,
            "focus": Tokens.focus,
            "outline": Tokens.outline,
            "status": Tokens.status,
            "danger": Tokens.danger,
            "radius": Tokens.radius,
            "space": Tokens.space,
            "type": Tokens.type,
            "motion": Tokens.motion,
            "sourceThemeId": Tokens.sourceThemeId
        })
        if (Tokens.accessibility !== undefined && Tokens.accessibility.reducedMotion !== undefined) {
            Tk.Theme.reducedMotion = Tokens.accessibility.reducedMotion === true
        }
    }

    property Connections _tokens: Connections {
        target: Tokens
        function onTokensChanged() { bridge.sync() }
    }

    onActiveChanged: sync()
    Component.onCompleted: sync()
}
