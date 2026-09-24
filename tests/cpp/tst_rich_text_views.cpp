// SPDX-License-Identifier: LGPL-3.0-or-later
// Accessibility, clipboard, pointer, commands and bounded tiles of RichTextArea.
#include "clipboard_service.h"
#include "command_context.h"
#include "command_registry.h"
#include "rich_text_scene.h"
#include "rich_text_spans.h"
#include "transfer_payload.h"
#include <QAccessible>
#include <QMimeData>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;
using namespace QindaTK::RichTextScene;

namespace {
class MemoryClipboard final : public ClipboardAdapter {
public:
    const QMimeData *mimeData() const override { return m_data.get(); }
    bool setMimeData(QMimeData *data) override
    {
        m_data.reset(data);
        emit changed();
        return true;
    }
    std::unique_ptr<QMimeData> m_data;
};
} // namespace

class RichTextViewsTest : public QObject {
    Q_OBJECT
private slots:
    void accessibleTextInterfaceUsesPlainTextOffsets()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.document.insertText(scene.document.startPosition(), QStringLiteral("alpha beta\nשלום gamma"));
        const auto ps = scene.document.paragraphsInOrder();
        scene.area->setSelection({{ps.at(1)->id, 1}, {ps.at(1)->id, 4}});
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(scene.area);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::EditableText);
        QAccessibleTextInterface *text = iface->textInterface();
        QVERIFY(text);
        QCOMPARE(text->characterCount(), 21);
        QCOMPARE(text->text(0, 5), QStringLiteral("alpha"));
        QCOMPARE(text->cursorPosition(), 11 + 4);
        int start = -1, end = -1;
        text->selection(0, &start, &end);
        QCOMPARE(start, 12);
        QCOMPARE(end, 15);
        QCOMPARE(text->text(start, end), QStringLiteral("לום"));
        text->setCursorPosition(6);
        QCOMPARE(scene.area->caretParagraphId(), ps.at(0)->id.toString(QUuid::WithoutBraces));
        QCOMPARE(scene.area->caretOffset(), 6);
    }

    void clipboardRoundTripKeepsStructureAndFormats()
    {
        MemoryClipboard clipboard;
        Scene source;
        QVERIFY(source.create(true));
        buildHardFixture(source.document);
        source.area->setClipboardAdapter(&clipboard);
        const auto ps = source.document.paragraphsInOrder();
        CharFormatPatch bold;
        bold.weight = 700;
        QVERIFY(source.document.applyCharFormat({ps.at(1)->id, 0}, {ps.at(1)->id, 2}, bold));
        source.area->setSelection({{ps.at(1)->id, 0}, {ps.at(3)->id, 2}});
        QVERIFY(source.area->invokeCommand(QStringLiteral("edit.copy")));
        const QMimeData *mime = clipboard.mimeData();
        QVERIFY(mime->hasFormat(QString::fromLatin1(TransferPayloadCodec::InternalMime)));
        QCOMPARE(mime->text(), source.area->selectedText());
        QVERIFY(mime->html().contains(QStringLiteral("font-weight:700")));

        Scene target;
        QVERIFY(target.create(false));
        target.area->setClipboardAdapter(&clipboard);
        target.type(QStringLiteral(">"));
        QVERIFY(target.area->invokeCommand(QStringLiteral("edit.paste")));
        const auto pasted = target.document.paragraphsInOrder();
        QCOMPARE(pasted.size(), 3);
        QCOMPARE(target.document.charFormat(Spans::formatAt(pasted.first()->spans, 1)).weight, 700);
        QVERIFY(pasted.first()->id != ps.at(1)->id);
        QCOMPARE(target.document.undoText(), QStringLiteral("Paste"));
        QVERIFY(target.document.undo());
        QCOMPARE(target.document.text(), QStringLiteral(">"));

        // Plain text from another application takes the caret's typing format.
        auto *plain = new QMimeData;
        plain->setText(QStringLiteral("one\ntwo"));
        clipboard.setMimeData(plain);
        QVERIFY(target.area->invokeCommand(QStringLiteral("text.bold")));
        QVERIFY(target.area->invokeCommand(QStringLiteral("edit.paste")));
        QCOMPARE(target.document.text(), QStringLiteral(">one\ntwo"));
        QCOMPARE(target.document.charFormat(Spans::formatAt(target.document.paragraphsInOrder().first()->spans, 1)).weight, 700);
        // HTML from a browser keeps runs and links.
        auto *html = new QMimeData;
        html->setHtml(QStringLiteral("<p><b>bold</b> and <a href=\"https://example.org\">link</a></p>"));
        clipboard.setMimeData(html);
        target.area->selectAll();
        QVERIFY(target.area->invokeCommand(QStringLiteral("edit.paste")));
        const Paragraph *p = target.document.paragraphsInOrder().first();
        QCOMPARE(p->text, QStringLiteral("bold and link"));
        QCOMPARE(target.document.charFormat(Spans::formatAt(p->spans, 0)).weight, 700);
        QCOMPARE(target.document.charFormat(Spans::formatAt(p->spans, 10)).href, QStringLiteral("https://example.org"));
    }

    void pointerSelectsAcrossPagesAndWords()
    {
        Scene scene;
        QVERIFY(scene.create(true, {900, 1300}));
        buildLines(scene.document, 45);
        scene.area->setZoom(0.5);
        scene.area->layout()->paginateAll();
        const auto ps = scene.document.paragraphsInOrder();
        const QPointF from = scene.pointAt({ps.at(10)->id, 1});
        const QPointF to = scene.pointAt({ps.at(25)->id, 2});
        QVERIFY(scene.area->pageAtY(from.y()) == 0 && scene.area->pageAtY(to.y()) == 1);
        QTest::mousePress(scene.window.get(), Qt::LeftButton, {}, from.toPoint());
        QTest::mouseMove(scene.window.get(), to.toPoint());
        QTest::mouseRelease(scene.window.get(), Qt::LeftButton, {}, to.toPoint());
        QCOMPARE(scene.area->selection().anchor.paragraph, ps.at(10)->id);
        QCOMPARE(scene.area->selection().focus.paragraph, ps.at(25)->id);
        QVERIFY(!scene.area->layout()->selectionRects(0, scene.area->selection()).isEmpty());
        QVERIFY(!scene.area->layout()->selectionRects(1, scene.area->selection()).isEmpty());
        const QPointF word = scene.pointAt({ps.at(12)->id, 1});
        QTest::mouseDClick(scene.window.get(), Qt::LeftButton, {}, word.toPoint());
        QCOMPARE(scene.area->selectedText(), QStringLiteral("L12"));
    }

    void commandRegistryDrivesTheSameCommands()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.document.insertText(scene.document.startPosition(), QStringLiteral("format me"));
        scene.area->selectAll();
        CommandContext context;
        QObject owner;
        const QString scope = context.pushScope(QStringLiteral("editor"), &owner);
        QVERIFY(context.activate(scope));
        CommandRegistry registry;
        registry.setContext(&context);
        QVERIFY(scene.area->registerCommands(&registry, scope));
        QVERIFY(registry.query(QStringLiteral("text.bold")).value(QStringLiteral("enabled")).toBool());
        QVERIFY(registry.trigger(QStringLiteral("text.bold")));
        QCOMPARE(scene.area->formatState()->bold(), 2);
        QVERIFY(registry.query(QStringLiteral("text.bold")).value(QStringLiteral("checked")).toBool());
        QVERIFY(registry.trigger(QStringLiteral("edit.undo")));
        QCOMPARE(scene.area->formatState()->bold(), 0);
    }

    void ctrlClickActivatesLinks()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        scene.document.insertText(scene.document.startPosition(), QStringLiteral("see the manual"));
        const QUuid id = scene.document.startPosition().paragraph;
        CharFormatPatch link;
        link.href = QStringLiteral("https://example.org/manual");
        QVERIFY(scene.document.applyCharFormat({id, 8}, {id, 14}, link));
        QSignalSpy activated(scene.area, &RichTextArea::linkActivated);
        const QPoint point = scene.pointAt({id, 10}).toPoint() + QPoint(3, 0);
        QTest::mouseClick(scene.window.get(), Qt::LeftButton, Qt::ControlModifier, point);
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("https://example.org/manual"));
    }

    void visibleTilesStayBoundedOnFiveHundredPages()
    {
        Scene scene;
        QVERIFY(scene.create(true, {900, 700}));
        buildLines(scene.document, 500 * LinesPerPage);
        scene.area->layout()->paginateAll();
        QCOMPARE(scene.area->pageCount(), 500);
        int most = 0;
        for (const qreal fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            scene.area->setContentY(fraction * (scene.area->contentHeight() - scene.area->height()));
            most = qMax(most, scene.area->visiblePages()->rowCount());
        }
        QVERIFY2(most <= 2, qPrintable(QString::number(most)));
        QVERIFY(scene.area->goToPage(420));
        QCOMPARE(scene.area->currentPage(), 420);
        QVERIFY(scene.area->visiblePages()->rowCount() <= 2);
        QCOMPARE(scene.area->visiblePages()->data(scene.area->visiblePages()->index(0, 0),
                                                  RichTextVisiblePages::PageIndexRole).toInt(), 420);
        qInfo("MEASURE 500 pages: at most %d visible page tiles while scrolling a 900x700 view", most);
    }
};

QTEST_MAIN(RichTextViewsTest)
#include "tst_rich_text_views.moc"
