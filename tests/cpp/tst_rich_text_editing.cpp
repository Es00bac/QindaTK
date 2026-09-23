// SPDX-License-Identifier: LGPL-3.0-or-later
// Keyboard, input-method and page-crossing edits through RichTextArea.
#include "rich_text_scene.h"
#include "rich_text_spans.h"
#include <QInputMethodEvent>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;
using namespace QindaTK::RichTextScene;

class RichTextEditingTest : public QObject {
    Q_OBJECT
private slots:
    void typingIsOneUndoStepPerRun()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.type(QStringLiteral("abc"));
        QTest::keyClick(scene.window.get(), Qt::Key_Space);
        scene.type(QStringLiteral("def"));
        QCOMPARE(scene.document.text(), QStringLiteral("abc def"));
        QCOMPARE(scene.area->caretOffset(), 7);
        QTest::keyClick(scene.window.get(), Qt::Key_Left);
        scene.type(QStringLiteral("X"));
        QCOMPARE(scene.document.text(), QStringLiteral("abc deXf"));
        QTest::keyClick(scene.window.get(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(scene.document.text(), QStringLiteral("abc def"));
        QCOMPARE(scene.area->caretOffset(), 6);
        QTest::keyClick(scene.window.get(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(scene.document.text(), QString());
        QTest::keyClick(scene.window.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(scene.document.text(), QStringLiteral("abc def"));
    }

    void inputMethodPreeditCommitAndReplacement()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.type(QStringLiteral("ab"));
        const QString before = scene.document.toJson();
        QInputMethodEvent preedit(QStringLiteral("にほ"), {{QInputMethodEvent::Cursor, 1, 1, {}}});
        QCoreApplication::sendEvent(scene.area, &preedit);
        QCOMPARE(scene.area->preeditText(), QStringLiteral("にほ"));
        QCOMPARE(scene.document.toJson(), before); // composition is view state only
        const QRectF composing = scene.area->inputMethodQuery(Qt::ImCursorRectangle).toRectF();
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("日本"));
        QCoreApplication::sendEvent(scene.area, &commit);
        QCOMPARE(scene.area->preeditText(), QString());
        QCOMPARE(scene.document.text(), QStringLiteral("ab日本"));
        QVERIFY(composing.left() > scene.area->pageOrigin(0).x());
        // Replacement: the IME rewrites the character before the caret.
        QInputMethodEvent replace;
        replace.setCommitString(QStringLiteral("語"), -1, 1);
        QCoreApplication::sendEvent(scene.area, &replace);
        QCOMPARE(scene.document.text(), QStringLiteral("ab日語"));
        QCOMPARE(scene.area->inputMethodQuery(Qt::ImSurroundingText).toString(), QStringLiteral("ab日語"));
        QCOMPARE(scene.area->inputMethodQuery(Qt::ImCursorPosition).toInt(), 4);
        // Composing over a selection replaces it; the whole gesture undoes once.
        scene.area->setSelection({{scene.document.startPosition().paragraph, 0}, {scene.document.startPosition().paragraph, 2}});
        QInputMethodEvent start(QStringLiteral("k"), {});
        QCoreApplication::sendEvent(scene.area, &start);
        QInputMethodEvent done;
        done.setCommitString(QStringLiteral("か"));
        QCoreApplication::sendEvent(scene.area, &done);
        QCOMPARE(scene.document.text(), QStringLiteral("か日語"));
        QVERIFY(scene.document.undo());
        QCOMPARE(scene.document.text(), QStringLiteral("ab日語"));
        // Undo restored the selection; with a caret, cancelling a composition
        // leaves the document untouched.
        scene.area->setSelection({scene.document.endPosition(), scene.document.endPosition()});
        QInputMethodEvent cancel(QStringLiteral("x"), {});
        QCoreApplication::sendEvent(scene.area, &cancel);
        QInputMethodEvent empty;
        QCoreApplication::sendEvent(scene.area, &empty);
        QCOMPARE(scene.document.text(), QStringLiteral("ab日語"));
    }

    void backspaceAndDeleteRemoveWholeGraphemes()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.document.insertText(scene.document.startPosition(),
                                  QStringLiteral("x\U0001F469‍\U0001F469‍\U0001F467\U0001F1FA\U0001F1F8éy"));
        const QUuid id = scene.document.startPosition().paragraph;
        scene.area->setSelection({{id, 15}, {id, 15}}); // after e + U+0301
        QTest::keyClick(scene.window.get(), Qt::Key_Backspace);
        QCOMPARE(scene.document.text(), QStringLiteral("x\U0001F469‍\U0001F469‍\U0001F467\U0001F1FA\U0001F1F8y"));
        QTest::keyClick(scene.window.get(), Qt::Key_Backspace); // the whole flag
        QCOMPARE(scene.document.text(), QStringLiteral("x\U0001F469‍\U0001F469‍\U0001F467y"));
        scene.area->setSelection({{id, 1}, {id, 1}});
        QTest::keyClick(scene.window.get(), Qt::Key_Delete); // the whole ZWJ family
        QCOMPARE(scene.document.text(), QStringLiteral("xy"));
    }

    void editsAcrossAPageBreakAndUndo()
    {
        Scene scene;
        QVERIFY(scene.create(true));
        buildLines(scene.document, 45);
        scene.area->layout()->paginateAll();
        const auto ps = scene.document.paragraphsInOrder();
        QCOMPARE(scene.area->pageCount(), 3);
        QCOMPARE(scene.area->layout()->pageOfPosition({ps.at(20)->id, 0}), 1);
        // Backspace at the top of page 1 joins the last paragraph of page 0.
        scene.area->setSelection({{ps.at(20)->id, 0}, {ps.at(20)->id, 0}});
        QTest::keyClick(scene.window.get(), Qt::Key_Backspace);
        QCOMPARE(scene.document.paragraphCount(), 44);
        QCOMPARE(scene.area->caretParagraphId(), ps.at(19)->id.toString(QUuid::WithoutBraces));
        QCOMPARE(scene.area->caretOffset(), int(mixedLabel(19).size()));
        QCOMPARE(scene.area->currentPage(), 0);
        QTest::keyClick(scene.window.get(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(scene.document.paragraphCount(), 45);
        QCOMPARE(scene.area->caretParagraphId(), ps.at(20)->id.toString(QUuid::WithoutBraces));
        QCOMPARE(scene.area->currentPage(), 1);
        // Shift+Down from the last line of page 0 extends onto page 1.
        scene.area->setSelection({{ps.at(19)->id, 2}, {ps.at(19)->id, 2}});
        QTest::keyClick(scene.window.get(), Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(scene.area->caretParagraphId(), ps.at(20)->id.toString(QUuid::WithoutBraces));
        QVERIFY(!scene.area->layout()->selectionRects(0, scene.area->selection()).isEmpty());
        QVERIFY(!scene.area->layout()->selectionRects(1, scene.area->selection()).isEmpty());
        scene.type(QStringLiteral("#"));
        QCOMPARE(scene.document.paragraphCount(), 44);
        QVERIFY(scene.document.paragraphsInOrder().at(19)->text.startsWith(QStringLiteral("L1#")));
        QVERIFY(scene.document.undo());
        QCOMPARE(scene.document.paragraphCount(), 45);
        QCOMPARE(scene.document.paragraphsInOrder().at(20)->text, mixedLabel(20));
    }

    void selectionSurvivesReflowFromAnotherView()
    {
        Scene scene;
        QVERIFY(scene.create(true));
        buildLines(scene.document, 60);
        scene.area->layout()->paginateAll();
        const Paragraph *target = scene.document.paragraphsInOrder().at(38);
        scene.area->setSelection({{target->id, 0}, {target->id, 3}});
        const QString selected = scene.area->selectedText();
        QCOMPARE(scene.area->currentPage(), 1);
        // Another view inserts five paragraphs at the top: pages reflow, the
        // logical selection does not move.
        QVERIFY(scene.document.insertText(scene.document.startPosition(), QStringLiteral("a\nb\nc\nd\ne\n")));
        QCOMPARE(scene.area->selectedText(), selected);
        QCOMPARE(scene.area->caretParagraphId(), target->id.toString(QUuid::WithoutBraces));
        QCOMPARE(scene.area->currentPage(), 2);
    }

    void formattingIsOneStepAndKeepsSelectionAndFocus()
    {
        Scene scene;
        QVERIFY(scene.create(true));
        buildLines(scene.document, 8);
        const auto ps = scene.document.paragraphsInOrder();
        const Selection range{{ps.at(1)->id, 1}, {ps.at(4)->id, 2}};
        scene.area->setSelection(range);
        QCOMPARE(scene.area->formatState()->bold(), 0);
        QVERIFY(scene.area->invokeCommand(QStringLiteral("text.bold")));
        QCOMPARE(scene.area->formatState()->bold(), 2);
        QCOMPARE(scene.document.undoText(), QStringLiteral("Bold"));
        QVERIFY(scene.area->hasActiveFocus());
        QCOMPARE(scene.area->selection().anchor, range.anchor);
        QCOMPARE(scene.area->selection().focus, range.focus);
        scene.area->setSelection({{ps.at(0)->id, 0}, {ps.at(2)->id, 1}});
        QCOMPARE(scene.area->formatState()->bold(), 1); // mixed
        QVERIFY(scene.area->invokeCommand(QStringLiteral("text.bold"))); // mixed -> on
        QCOMPARE(scene.area->formatState()->bold(), 2);
        QVERIFY(scene.document.undo());
        QCOMPARE(scene.area->formatState()->bold(), 1);
        QVERIFY(scene.document.undo());
        scene.area->setSelection(range);
        QCOMPARE(scene.area->formatState()->bold(), 0);
        QVERIFY(scene.area->invokeCommand(QStringLiteral("paragraph.numberedList")));
        QCOMPARE(scene.area->formatState()->list(), QStringLiteral("number"));
        QCOMPARE(scene.area->layout()->listMarker(ps.at(4)->id), QStringLiteral("4."));
        QVERIFY(scene.area->invokeCommand(QStringLiteral("paragraph.align"), {{QStringLiteral("value"), QStringLiteral("center")}}));
        QCOMPARE(scene.area->formatState()->alignment(), QStringLiteral("center"));
    }

    void pendingFormatAppliesToTheNextTypedText()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.type(QStringLiteral("ab"));
        QVERIFY(scene.area->invokeCommand(QStringLiteral("text.italic")));
        QCOMPARE(scene.area->formatState()->italic(), 2);
        scene.type(QStringLiteral("cd"));
        const Paragraph *p = scene.document.paragraphsInOrder().first();
        QCOMPARE(scene.document.charFormat(Spans::formatAt(p->spans, 0)).italic, qint8(-1));
        QCOMPARE(scene.document.charFormat(Spans::formatAt(p->spans, 2)).italic, qint8(1));
        QCOMPARE(scene.document.charFormat(Spans::formatAt(p->spans, 3)).italic, qint8(1));
        QTest::keyClick(scene.window.get(), Qt::Key_Home);
        QCOMPARE(scene.area->formatState()->italic(), 0);
    }

    void enterEndsAnEmptyListItemAndTabChangesLevel()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.type(QStringLiteral("one"));
        QVERIFY(scene.area->invokeCommand(QStringLiteral("paragraph.bulletList")));
        QTest::keyClick(scene.window.get(), Qt::Key_Return);
        QTest::keyClick(scene.window.get(), Qt::Key_Tab);
        QCOMPARE(scene.document.paragraphsInOrder().last()->format.listLevel, 1);
        QTest::keyClick(scene.window.get(), Qt::Key_Tab, Qt::ShiftModifier);
        QCOMPARE(scene.document.paragraphsInOrder().last()->format.listLevel, 0);
        QTest::keyClick(scene.window.get(), Qt::Key_Return); // empty item: leave the list
        QCOMPARE(scene.document.paragraphCount(), 2);
        QVERIFY(scene.document.paragraphsInOrder().last()->format.listId.isEmpty());
    }
};

QTEST_MAIN(RichTextEditingTest)
#include "tst_rich_text_editing.moc"
