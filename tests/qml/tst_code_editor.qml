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

    function test_multi_cursor_and_completion() {
        source.text = "a\nb"
        editor.editor.cursorPosition = 1
        editor.editor.forceActiveFocus()
        verify(editor.addCursorBelow())
        compare(editor.additionalCursors.length, 1)
        keyClick(Qt.Key_X)
        compare(source.text, "ax\nbx")
        verify(source.undo())
        compare(source.text, "a\nb")
        editor.clearAdditionalCursors()
        const revision = source.revision
        verify(source.publishCompletions(revision, 2, 3,
                                         [{label: "bee", insertText: "bee"}]))
        editor.editor.cursorPosition = 3
        tryCompare(editor, "cursorPosition", 3)
        verify(editor.acceptCompletion(0))
        compare(source.text, "a\nbee")
        verify(!source.publishCompletions(revision, 2, 3, []))
        editor.editor.cursorPosition = 0
        mouseClick(editor.editor, 20, 22, Qt.LeftButton, Qt.AltModifier)
        compare(editor.additionalCursors.length, 1)
    }

    function test_fold_collapses_native_view_without_changing_source() {
        source.text = "one\ntwo\nthree\nfour\nfive\nsix\n"
        const original = source.text
        wait(50)
        const expandedHeight = editor.editor.contentHeight
        verify(source.foldLines(1, 5))
        tryCompare(source, "visualLineCount", 3)
        tryVerify(function() { return editor.editor.contentHeight < expandedHeight })
        compare(source.text, original)
        compare(source.lineForVisualRow(1), 6)
        verify(source.unfoldLines(1))
        tryVerify(function() { return editor.editor.contentHeight >= expandedHeight })
        compare(source.text, original)
    }
}
