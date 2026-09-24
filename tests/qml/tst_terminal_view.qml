// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "TerminalView"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Tk.TerminalSession { id: terminalSession }
    Component {
        id: viewComponent
        Tk.TerminalView {
            width: 420
            height: 280
            session: terminalSession
        }
    }

    function test_instantiation_is_inert_and_sizes_the_pty_grid() {
        const view = createTemporaryObject(viewComponent, this)
        verify(view)
        verify(!terminalSession.running)
        verify(terminalSession.rows >= 2 && terminalSession.rows <= 100)
        verify(terminalSession.columns >= 2 && terminalSession.columns <= 240)
        const viewport = findChild(view, "terminalViewport")
        verify(viewport !== null)
        verify(findChild(view, "terminalInput") !== null)
        verify(findChild(view, "terminalCursor") !== null)
        verify(findChild(view, "terminalEmptyPrompt").visible)
        compare(view.Accessible.name, view.tooltip)
        verify(view._rows.length >= terminalSession.rows)
    }
}
