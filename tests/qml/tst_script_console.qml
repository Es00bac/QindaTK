// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "ScriptConsole"
    visible: true
    when: windowShown
    width: 700
    height: 500

    Tk.ScriptRuntime {
        id: runtime
        documentSnapshot: ({title: "Fixture"})
    }
    Tk.ScriptConsole {
        id: consoleView
        width: 680
        height: 480
        runtime: runtime
        sourceText: "app.print(app.inspect('title'));"
    }

    function test_run_shows_real_runtime_output() {
        const run = findChild(consoleView, "scriptRun")
        const output = findChild(consoleView, "scriptOutput")
        verify(run !== null)
        verify(output !== null)
        mouseClick(run)
        tryCompare(runtime, "status", "completed")
        compare(runtime.output, "Fixture")
        compare(output.text, "Fixture")
        verify(findChild(consoleView, "scriptCancel") !== null)
        verify(findChild(consoleView, "scriptReplay") !== null)
    }
}
