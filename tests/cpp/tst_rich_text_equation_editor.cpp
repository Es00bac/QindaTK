// SPDX-License-Identifier: LGPL-3.0-or-later
// Visual equation editing (WP-052, OFF-09): a caret and selection in the
// expression tree, typing with scripts and fractions, palette templates,
// deletion, hit testing on the laid-out equation, and the round trip
// between visual and source edits; plus Tk.EquationCanvas driven by keys.
#include "equation_canvas.h"
#include "rich_text_equation_editor.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText::Equation;

namespace {

// The editor's tree is the tree its own source parses to.
bool roundTrips(const Editor &editor)
{
    const Parsed parsed = parse(editor.source());
    return parsed.root && normalized(*parsed.root) == editor.root();
}

} // namespace

class RichTextEquationEditorTest : public QObject {
    Q_OBJECT
private slots:
    void typingBuildsTheTree()
    {
        Editor editor;
        QVERIFY(editor.insertText(QStringLiteral("x^2")));
        QCOMPARE(editor.source(), QStringLiteral("x^2"));
        QCOMPARE(editor.caretPath(), QStringLiteral("0.1:1")); // in the superscript
        QVERIFY(editor.move(Editor::Move::Right));             // out of it
        QCOMPARE(editor.caretPath(), QStringLiteral(":1"));
        QVERIFY(editor.insertText(QStringLiteral("+1/2")));
        QCOMPARE(editor.source(), QStringLiteral("x^2+\\frac{1}{2}"));
        QCOMPARE(editor.caretPath(), QStringLiteral("2.1:1"));  // in the denominator
        QVERIFY(editor.insertText(QStringLiteral("_i")));
        QCOMPARE(editor.source(), QStringLiteral("x^2+\\frac{1}{2_i}"));
        QVERIFY(roundTrips(editor));
        QCOMPARE(linearText(denormalized(editor.root())), QStringLiteral("x^2 + 1/(2_i)"));
        // Digits join a number, "3" "." "1" reads as 3.1.
        Editor numbers;
        QVERIFY(numbers.insertText(QStringLiteral("12+3.1")));
        QCOMPARE(numbers.source(), QStringLiteral("12+3.1"));
        QCOMPARE(numbers.root().children.size(), std::size_t(3));
        // A script needs a base; a refused edit changes nothing.
        Editor empty;
        QVERIFY(!empty.insertText(QStringLiteral("^")));
        QCOMPARE(empty.source(), QString());
        QVERIFY(!empty.insertText(QStringLiteral("a#")));
        QCOMPARE(empty.source(), QString());
    }

    void caretMovesThroughSlots()
    {
        Editor editor;
        QVERIFY(editor.setSource(QStringLiteral("\\frac{ab}{c}+\\sqrt[3]{x}")));
        editor.setCaret({{}, 0});
        QVERIFY(editor.move(Editor::Move::Right));
        QCOMPARE(editor.caretPath(), QStringLiteral("0.0:0")); // into the numerator
        QVERIFY(editor.move(Editor::Move::Right));
        QCOMPARE(editor.caretPath(), QStringLiteral("0.0:1"));
        QVERIFY(editor.move(Editor::Move::Down));
        QCOMPARE(editor.caretPath(), QStringLiteral("0.1:1")); // denominator, clamped
        QVERIFY(editor.move(Editor::Move::Up));
        QCOMPARE(editor.caretPath(), QStringLiteral("0.0:1"));
        QVERIFY(editor.move(Editor::Move::End));
        QVERIFY(editor.move(Editor::Move::Right));
        QCOMPARE(editor.caretPath(), QStringLiteral("0.1:0"));
        QVERIFY(editor.move(Editor::Move::Right));
        QVERIFY(editor.move(Editor::Move::Right));
        QCOMPARE(editor.caretPath(), QStringLiteral(":1"));   // after the fraction
        QVERIFY(editor.move(Editor::Move::Right));
        QVERIFY(editor.move(Editor::Move::Right));
        QCOMPARE(editor.caretPath(), QStringLiteral("2.1:0")); // the root index comes first
        QVERIFY(editor.move(Editor::Move::Right));
        QVERIFY(editor.move(Editor::Move::Right));
        QCOMPARE(editor.caretPath(), QStringLiteral("2.0:0"));
        QVERIFY(editor.move(Editor::Move::Left));
        QCOMPARE(editor.caretPath(), QStringLiteral("2.1:1"));
        // A selection stays in its row.
        editor.setCaret({{0, 0}, 0});
        QVERIFY(editor.move(Editor::Move::End, true));
        QCOMPARE(editor.selectedSource(), QStringLiteral("ab"));
    }

    void templatesAndDeletion()
    {
        Editor editor;
        QVERIFY(editor.insertText(QStringLiteral("a+b")));
        editor.selectAll();
        QVERIFY(editor.insertSymbol(QStringLiteral("sqrt")));
        QCOMPARE(editor.source(), QStringLiteral("\\sqrt{a+b}"));
        QCOMPARE(editor.caretPath(), QStringLiteral(":1"));
        QVERIFY(editor.insertSymbol(QStringLiteral("frac")));
        QCOMPARE(editor.source(), QStringLiteral("\\sqrt{a+b}\\frac{}{}"));
        QCOMPARE(editor.caretPath(), QStringLiteral("1.0:0")); // first empty slot
        QVERIFY(editor.insertText(QStringLiteral("1")));
        QVERIFY(editor.move(Editor::Move::Down));
        QVERIFY(editor.insertText(QStringLiteral("n")));
        QCOMPARE(editor.source(), QStringLiteral("\\sqrt{a+b}\\frac{1}{n}"));
        QVERIFY(editor.insertSymbol(QStringLiteral("sup")));
        QVERIFY(editor.insertText(QStringLiteral("2")));
        QCOMPARE(editor.source(), QStringLiteral("\\sqrt{a+b}\\frac{1}{n^2}"));
        QVERIFY(editor.move(Editor::Move::Right));
        QVERIFY(editor.move(Editor::Move::Right));
        QVERIFY(editor.insertSymbol(QStringLiteral("sum")));
        QCOMPARE(editor.caretPath(), QStringLiteral("2.1:0")); // the lower limit
        QVERIFY(roundTrips(editor));

        // Backspace steps into a structure, then empties and unwraps it.
        Editor back;
        QVERIFY(back.setSource(QStringLiteral("x^{2}")));
        back.setCaret({{}, 1});
        QVERIFY(back.backspace());
        QCOMPARE(back.caretPath(), QStringLiteral("0.1:1"));
        QVERIFY(back.backspace());
        QCOMPARE(back.source(), QStringLiteral("x^{}"));
        QVERIFY(back.backspace());                            // at the start of the script: unwrap
        QCOMPARE(back.source(), QStringLiteral("x"));
        QCOMPARE(back.caretPath(), QStringLiteral(":1"));
        QVERIFY(back.setSource(QStringLiteral("\\frac{a}{b}")));
        back.setCaret({{0, 0}, 0});
        QVERIFY(back.backspace());
        QCOMPARE(back.source(), QStringLiteral("ab"));
        QVERIFY(back.deleteForward());
        QCOMPARE(back.source(), QStringLiteral("b"));
    }

    void sourceAndVisualEditsRoundTrip()
    {
        // Source edits re-derive the tree; the caret survives where it can.
        Editor editor;
        QVERIFY(editor.setSource(QStringLiteral("E = \\sum_{i=1}^{n} q_i \\Delta t")));
        QVERIFY(roundTrips(editor));
        editor.setCaret({{2, 2}, 1});
        QCOMPARE(editor.caretPath(), QStringLiteral("2.2:1"));
        QVERIFY(editor.insertText(QStringLiteral("0")));
        QCOMPARE(editor.source(), QStringLiteral("E=\\sum_{i=1}^{n0}q_i\\Delta t"));
        ParseError error;
        QVERIFY(!editor.setSource(QStringLiteral("\\begin{x}"), &error));
        QCOMPARE(error.code, ErrorCode::Unsupported);
        QCOMPARE(editor.source(), QStringLiteral("E=\\sum_{i=1}^{n0}q_i\\Delta t")); // unchanged
        QVERIFY(editor.setSource(QStringLiteral("E=m")));
        QCOMPARE(editor.caretPath(), QStringLiteral(":3")); // the old row is gone: the end

        // Hit testing on the editing layout lands in the slot under the point.
        QVERIFY(editor.setSource(QStringLiteral("\\frac{a}{b}")));
        const Box box = layout(editor.root(), 20, true);
        double numeratorY = 0, denominatorY = 0;
        for (const Stop &stop : box.stops) {
            if (stop.row == QVector<int>{0, 0}) numeratorY = (stop.top + stop.bottom) / 2;
            if (stop.row == QVector<int>{0, 1}) denominatorY = (stop.top + stop.bottom) / 2;
        }
        QVERIFY(numeratorY < denominatorY);
        QCOMPARE(Editor::caretAt(box, QPointF(box.width / 2, denominatorY)).row, (QVector<int>{0, 1}));
        QCOMPARE(Editor::caretAt(box, QPointF(box.width / 2, numeratorY)).row, (QVector<int>{0, 0}));
        // An empty slot has a placeholder in editing layouts only.
        QVERIFY(editor.setSource(QStringLiteral("\\frac{}{}")));
        QCOMPARE(layout(editor.root(), 20, true).placeholders.size(), 2);
        QVERIFY(layout(editor.root(), 20).placeholders.isEmpty());
    }

    void canvasEditsFromKeys()
    {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QindaTK as Tk\nTk.EquationCanvas { width: 300; height: 120; focus: true }",
                          QUrl(QStringLiteral("qrc:/canvas.qml")));
        std::unique_ptr<QObject> object(component.create());
        auto *canvas = qobject_cast<EquationCanvas *>(object.get());
        QVERIFY2(canvas, qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(320, 140);
        canvas->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        canvas->forceActiveFocus();
        QSignalSpy edited(canvas, &EquationCanvas::edited);
        canvas->setSource(QStringLiteral("a"));
        QCOMPARE(edited.count(), 0); // a source set from outside is not an edit
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, '^');
        QTest::keyClick(&window, '2');
        QCOMPARE(canvas->source(), QStringLiteral("a^2"));
        QCOMPARE(edited.count(), 2);
        QCOMPARE(edited.last().first().toString(), QStringLiteral("a^2"));
        QTest::keyClick(&window, Qt::Key_Backspace);
        QCOMPARE(canvas->source(), QStringLiteral("a^{}"));
        QCOMPARE(canvas->linearText(), QStringLiteral("a^()"));
        QVERIFY(canvas->caretRectangle().height() > 0);
        QVERIFY(canvas->implicitWidth() > 0);
        // A click puts the caret at the nearest stop.
        QVERIFY(canvas->setCaretAt(1, canvas->height() / 2));
        QCOMPARE(canvas->caretPath(), QStringLiteral(":0"));
        canvas->setReadOnly(true);
        QVERIFY(!canvas->insertText(QStringLiteral("b")));
        QCOMPARE(canvas->source(), QStringLiteral("a^{}"));
    }
};

QTEST_MAIN(RichTextEquationEditorTest)
#include "tst_rich_text_equation_editor.moc"
