// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "CodeEditor"
    when: windowShown
    width: 720
    height: 480

    Tk.CodeDocument { id: source; text: "let café = 42;\nlet fox = café;\n" }
    Tk.CodeEditor {
        id: editor
        width: 680
        height: 420
        document: source
        tooltip: "Test source"
    }

    function test_attached_native_buffer_and_search() {
        compare(editor.editor.text, source.text)
        verify(editor.goToLine(2))
        compare(editor.cursorPosition, source.text.indexOf("let fox"))
        editor.editor.forceActiveFocus()
        editor.editor.insert(editor.cursorPosition, "// ")
        compare(source.text.indexOf("// let fox"), 15)
        verify(editor.findNext("café"))
        compare(editor.editor.selectedText, "café")
        verify(source.undo())
        verify(source.text.indexOf("// let fox") < 0)
        const revision = source.revision
        verify(source.publishSyntaxTokens(revision, [{ start: 0, length: 3, kind: "keyword" }]))
        compare(source.revision, revision)
        compare(source.syntaxTokens.length, 1)
    }
}
