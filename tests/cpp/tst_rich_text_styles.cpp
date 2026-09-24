// SPDX-License-Identifier: LGPL-3.0-or-later
// Named styles (WP-051): inheritance, direct overrides, cycles, character and
// table styles, one-step undo, local repagination, persistence and commands.
#include "document_session.h"
#include "rich_text_codec.h"
#include "rich_text_layout.h"
#include "rich_text_scene.h"
#include "rich_text_session_adapter.h"
#include "rich_text_spans.h"
#include <QJsonArray>
#include <QJsonDocument>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;
using namespace QindaTK::RichTextScene;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

const Paragraph *para(const RichTextDocument &doc, int index) { return doc.paragraphsInOrder().at(index); }

QStringList ids(const RichTextDocument &doc, int from, int to)
{
    QStringList result;
    for (int i = from; i <= to; ++i) result.append(sid(para(doc, i)->id));
    return result;
}

QVariantMap group(const char *name, const QVariantMap &members) { return {{QLatin1String(name), members}}; }

struct Sheet {
    QString body;
    QString heading;
};

// "Body" (6 pt after, 12 pt text) and "Heading" (inherits Body; level 1, 16 pt bold).
Sheet buildSheet(RichTextDocument &doc)
{
    Sheet sheet;
    sheet.body = doc.createStyle(QStringLiteral("Body"), QStringLiteral("paragraph"));
    sheet.heading = doc.createStyle(QStringLiteral("Heading"), QStringLiteral("paragraph"), sheet.body);
    QVariantMap body = group("paragraph", {{QStringLiteral("spaceAfterPt"), 6}});
    body.insert(QStringLiteral("character"), QVariantMap{{QStringLiteral("sizePt"), 12}});
    doc.updateStyle(sheet.body, body);
    QVariantMap heading = group("paragraph", {{QStringLiteral("heading"), 1}});
    heading.insert(QStringLiteral("character"), QVariantMap{{QStringLiteral("weight"), 700}, {QStringLiteral("sizePt"), 16}});
    doc.updateStyle(sheet.heading, heading);
    return sheet;
}

CharFormat effective(const RichTextDocument &doc, const Paragraph *p, int offset = 0)
{
    const int index = p->spans.isEmpty() ? p->markFormat : Spans::formatAt(p->spans, offset);
    return doc.effectiveCharFormat(doc.charFormat(index), p->format);
}

} // namespace

class RichTextStylesTest : public QObject {
    Q_OBJECT
private slots:
    void parentChangesReachDependentsAndKeepOverrides()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Title\nFirst\nSecond\nThird"));
        const Sheet sheet = buildSheet(doc);
        QVERIFY(!sheet.body.isEmpty() && !sheet.heading.isEmpty());
        QVERIFY(doc.applyStyle(sheet.heading, ids(doc, 0, 0)));
        QVERIFY(doc.applyStyle(sheet.body, ids(doc, 1, 3)));
        QCOMPARE(para(doc, 0)->format.styleId, sheet.heading);
        QCOMPARE(para(doc, 0)->format.headingLevel, 1);
        QCOMPARE(para(doc, 0)->format.spaceAfterPt, 6.0); // inherited from Body
        QCOMPARE(para(doc, 2)->format.spaceAfterPt, 6.0);
        QCOMPARE(effective(doc, para(doc, 0)).sizePt, 16.0);
        QCOMPARE(effective(doc, para(doc, 0)).weight, 700);
        QCOMPARE(effective(doc, para(doc, 1)).sizePt, 12.0);
        // Direct formatting of a styled paragraph is an override.
        ParagraphFormatPatch direct;
        direct.spaceAfterPt = 12;
        const Position second{para(doc, 1)->id, 0};
        QVERIFY(doc.setParagraphFormat(second, second, direct));
        QCOMPARE(para(doc, 1)->format.overrides, quint32(StyleSpaceAfter));
        const QVariantList overrides = doc.directOverrides({sid(para(doc, 1)->id)});
        QCOMPARE(overrides.size(), 1);
        QCOMPARE(overrides.first().toMap().value(QStringLiteral("key")).toString(), QStringLiteral("paragraph.spaceAfterPt"));
        QCOMPARE(overrides.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("12 pt"));
        // A parent change reaches its own and its child's paragraphs, not the
        // override, as one undo step.
        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(doc.updateStyle(sheet.body, group("paragraph", {{QStringLiteral("spaceAfterPt"), 9}})));
        QCOMPARE(doc.undoText(), QStringLiteral("Update style"));
        QCOMPARE(para(doc, 0)->format.spaceAfterPt, 9.0);
        QCOMPARE(para(doc, 1)->format.spaceAfterPt, 12.0);
        QCOMPARE(para(doc, 2)->format.spaceAfterPt, 9.0);
        QCOMPARE(para(doc, 3)->format.spaceAfterPt, 9.0);
        QCOMPARE(para(doc, 0)->format.headingLevel, 1);
        const QByteArray after = RichTextCodec::encode(doc);
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        QVERIFY(doc.redo());
        QCOMPARE(RichTextCodec::encode(doc), after);
        // Resetting the override hands the value back to the style.
        QVERIFY(doc.resetDirectFormatting({sid(para(doc, 1)->id)}, {QStringLiteral("paragraph.spaceAfterPt")}));
        QCOMPARE(para(doc, 1)->format.spaceAfterPt, 9.0);
        QCOMPARE(para(doc, 1)->format.overrides, 0u);
        QVERIFY(doc.directOverrides({sid(para(doc, 1)->id)}).isEmpty());
        // Removing a paragraph's style keeps the values it gave as direct formatting.
        QVERIFY(doc.applyStyle(QString(), ids(doc, 2, 2), QStringLiteral("paragraph")));
        QCOMPARE(para(doc, 2)->format.styleId, QString());
        QCOMPARE(para(doc, 2)->format.spaceAfterPt, 9.0);
        QVERIFY(doc.updateStyle(sheet.body, group("paragraph", {{QStringLiteral("spaceAfterPt"), 3}})));
        QCOMPARE(para(doc, 2)->format.spaceAfterPt, 9.0);
        QCOMPARE(para(doc, 3)->format.spaceAfterPt, 3.0);
    }

    void invalidStyleEditsChangeNothing()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Title\nBody"));
        const Sheet sheet = buildSheet(doc);
        const QString emphasis = doc.createStyle(QStringLiteral("Emphasis"), QStringLiteral("character"));
        QVERIFY(!emphasis.isEmpty());
        const QByteArray before = RichTextCodec::encode(doc);
        const QString revision = doc.revisionString();
        QVERIFY(!doc.updateStyle(sheet.body, {{QStringLiteral("parentId"), sheet.heading}})); // a cycle
        QVERIFY(doc.lastError().contains(QStringLiteral("cycle")));
        QVERIFY(!doc.updateStyle(sheet.heading, {{QStringLiteral("parentId"), emphasis}}));    // another kind
        QVERIFY(!doc.updateStyle(sheet.heading, {{QStringLiteral("parentId"), sheet.heading}}));
        QVERIFY(!doc.updateStyle(sheet.body, {{QStringLiteral("kind"), QStringLiteral("character")}}));
        QVERIFY(!doc.updateStyle(emphasis, group("paragraph", {{QStringLiteral("align"), QStringLiteral("center")}})));
        QVERIFY(!doc.updateStyle(sheet.body, group("character", {{QStringLiteral("sizePt"), 0}})));
        QVERIFY(!doc.updateStyle(sheet.body, group("paragraph", {{QStringLiteral("fancy"), 1}})));
        QVERIFY(!doc.updateStyle(QStringLiteral("missing"), {{QStringLiteral("name"), QStringLiteral("X")}}));
        QVERIFY(doc.createStyle(QStringLiteral("heading"), QStringLiteral("paragraph")).isEmpty()); // name taken
        QVERIFY(doc.createStyle(QStringLiteral("  "), QStringLiteral("paragraph")).isEmpty());
        QVERIFY(doc.createStyle(QStringLiteral("Frame"), QStringLiteral("object")).isEmpty());
        QVERIFY(doc.createStyle(QStringLiteral("Orphan"), QStringLiteral("paragraph"), QStringLiteral("missing")).isEmpty());
        QVERIFY(!doc.applyStyle(emphasis, {sid(QUuid::createUuid())}));
        QVERIFY(!doc.applyStyle(QStringLiteral("missing"), ids(doc, 0, 0)));
        QCOMPARE(RichTextCodec::encode(doc), before);
        QCOMPARE(doc.revisionString(), revision);
        // A style leaves the sheet only while nothing uses it.
        QVERIFY(doc.applyStyle(emphasis, ids(doc, 1, 1)));
        QVERIFY(doc.beginTransaction(QStringLiteral("Host step")));
        QVERIFY(bool(doc.apply(StepStyle{emphasis, *doc.style(emphasis), std::nullopt})));
        doc.cancelTransaction();
        QVERIFY(doc.style(emphasis));
        // A format naming an unknown character style never enters the document.
        CharFormatPatch bogus;
        bogus.styleId = QStringLiteral("missing");
        QVERIFY(!doc.applyCharFormat({para(doc, 0)->id, 0}, {para(doc, 0)->id, 5}, bogus));
        QCOMPARE(effective(doc, para(doc, 0)).styleId, QString());
    }

    void characterStylesInheritUnderDirectRuns()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("plain words here"));
        const QString emphasis = doc.createStyle(QStringLiteral("Emphasis"), QStringLiteral("character"));
        QVERIFY(doc.updateStyle(emphasis, group("character", {{QStringLiteral("italic"), true},
                                                              {QStringLiteral("color"), QStringLiteral("#aa0000")}})));
        const Paragraph *p = para(doc, 0);
        QVERIFY(doc.applyCharacterStyle({p->id, 6}, {p->id, 11}, emphasis)); // "words"
        QCOMPARE(effective(doc, p, 0).italic, qint8(-1));
        QCOMPARE(effective(doc, p, 6).italic, qint8(1));
        QCOMPARE(effective(doc, p, 6).color, QColor(QStringLiteral("#aa0000")));
        // Direct formatting on a run wins over its style.
        CharFormatPatch upright;
        upright.italic = 0;
        QVERIFY(doc.applyCharFormat({p->id, 6}, {p->id, 8}, upright));
        QCOMPARE(effective(doc, p, 6).italic, qint8(0));
        QCOMPARE(effective(doc, p, 9).italic, qint8(1));
        // A style change reaches its runs only; a null member inherits again.
        QVERIFY(doc.updateStyle(emphasis, group("character", {{QStringLiteral("italic"), QVariant()},
                                                              {QStringLiteral("weight"), 700}})));
        QCOMPARE(effective(doc, p, 9).italic, qint8(-1));
        QCOMPARE(effective(doc, p, 9).weight, 700);
        QCOMPARE(effective(doc, p, 0).weight, 0);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        const qreal defaultLine = layout.metrics(p->id)->lines.first().height;
        // A paragraph style's character members sit under the character style.
        const QString body = doc.createStyle(QStringLiteral("Body"), QStringLiteral("paragraph"));
        QVERIFY(doc.updateStyle(body, group("character", {{QStringLiteral("weight"), 300}, {QStringLiteral("sizePt"), 22}})));
        QVERIFY(doc.applyStyle(body, ids(doc, 0, 0)));
        QCOMPARE(effective(doc, p, 0).weight, 300);
        QCOMPARE(effective(doc, p, 9).weight, 700);
        QCOMPARE(effective(doc, p, 9).sizePt, 22.0);
        // The layout shapes the resolved (twice as large) font.
        layout.paginateAll();
        QVERIFY(layout.metrics(p->id)->lines.first().height > 1.5 * defaultLine);
    }

    void tableStylesDriveBordersAndBands()
    {
        RichTextDocument doc;
        doc.setPageSetup(fixturePage());
        const auto caret = doc.insertTable(doc.startPosition(), 3, 2, 1);
        QVERIFY(caret);
        const QUuid table = doc.paragraph(caret->paragraph)->table->id;
        fixLineBoxes(doc);
        const QString grid = doc.createStyle(QStringLiteral("Grid"), QStringLiteral("table"));
        QVERIFY(doc.updateStyle(grid, group("table", {{QStringLiteral("borderPt"), 1.5},
                                                      {QStringLiteral("headerBackground"), QStringLiteral("#eeeeee")}})));
        const QString banded = doc.createStyle(QStringLiteral("Banded grid"), QStringLiteral("table"), grid);
        QVERIFY(doc.updateStyle(banded, group("table", {{QStringLiteral("bandBackground"), QStringLiteral("#f5f5f5")}})));
        QVERIFY(doc.setTableStyle(sid(table), banded));
        QCOMPARE(doc.undoText(), QStringLiteral("Table style"));
        const QVariantMap snapshot = doc.tableSnapshot(sid(table));
        QCOMPARE(snapshot.value(QStringLiteral("styleId")).toString(), banded);
        QCOMPARE(snapshot.value(QStringLiteral("styles")).toList().size(), 3); // none, Banded grid, Grid
        const TableStyle resolved = doc.tableStyle(*doc.table(table));
        QCOMPARE(*resolved.borderPt, 1.5); // inherited from Grid
        QCOMPARE(*resolved.headerBackground, QColor(QStringLiteral("#eeeeee")));
        QCOMPARE(*resolved.bandBackground, QColor(QStringLiteral("#f5f5f5")));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        const qreal padding = 2 * 4.0 * PxPerPt;
        QCOMPARE(layout.page(0).items.at(0).kind, PageItem::TableRows);
        QCOMPARE(layout.page(0).items.at(0).rowHeights.at(0), 1.5 * PxPerPt + padding + LinePx);
        // The parent's border reaches the table in one undoable change.
        QVERIFY(doc.updateStyle(grid, group("table", {{QStringLiteral("borderPt"), 3}})));
        layout.paginateAll();
        QCOMPARE(layout.page(0).items.at(0).rowHeights.at(0), 3 * PxPerPt + padding + LinePx);
        QVERIFY(doc.undo());
        layout.paginateAll();
        QCOMPARE(layout.page(0).items.at(0).rowHeights.at(0), 1.5 * PxPerPt + padding + LinePx);
        const QString body = doc.createStyle(QStringLiteral("Body"), QStringLiteral("paragraph"));
        QVERIFY(!doc.setTableStyle(sid(table), body));
        QVERIFY(doc.setTableStyle(sid(table), QString()));
        QVERIFY(!doc.tableStyle(*doc.table(table)).borderPt.has_value());
    }

    void stylesPersistAndPasteOnce()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Title\nBody text\n"));
        const Sheet sheet = buildSheet(doc);
        QVERIFY(doc.applyStyle(sheet.heading, ids(doc, 0, 0)));
        QVERIFY(doc.applyStyle(sheet.body, ids(doc, 1, 1)));
        ParagraphFormatPatch centered;
        centered.alignment = Alignment::Center;
        QVERIFY(doc.setParagraphFormat({para(doc, 1)->id, 0}, {para(doc, 1)->id, 0}, centered));
        const QByteArray bytes = RichTextCodec::encode(doc);
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
        QCOMPARE(root.value(QStringLiteral("styles")).toArray().size(), 2);
        RichTextDocument reopened;
        QVERIFY(RichTextCodec::decode(bytes, reopened));
        QCOMPARE(RichTextCodec::encode(reopened), bytes);
        QCOMPARE(para(reopened, 1)->format.overrides, quint32(StyleAlignment));
        RichTextSessionAdapter adapter;
        adapter.setDocument(&doc);
        RichTextDocument session;
        QVERIFY(RichTextCodec::fromJsonObject(adapter.liveJson(), session));
        QCOMPARE(RichTextCodec::encode(session), bytes);
        // Documents without WP-051 structure stay version 1.
        RichTextDocument plain;
        plain.insertText(plain.startPosition(), QStringLiteral("x"));
        QCOMPARE(QJsonDocument::fromJson(RichTextCodec::encode(plain)).object().value(QStringLiteral("version")).toInt(), 1);
        // An unknown style reference or a parent cycle is refused with its field.
        QJsonObject broken = root;
        QJsonArray blocks = broken.value(QStringLiteral("blocks")).toArray();
        QJsonObject block = blocks.at(0).toObject();
        QJsonObject paragraph = block.value(QStringLiteral("paragraph")).toObject();
        QJsonObject format = paragraph.value(QStringLiteral("format")).toObject();
        format.insert(QStringLiteral("style"), QStringLiteral("missing"));
        paragraph.insert(QStringLiteral("format"), format);
        block.insert(QStringLiteral("paragraph"), paragraph);
        blocks.replace(0, block);
        broken.insert(QStringLiteral("blocks"), blocks);
        RichTextDocument target;
        OperationError error;
        QVERIFY(!RichTextCodec::fromJsonObject(broken, target, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("blocks[0].paragraph.format.style"));
        QJsonObject cyclic = root;
        QJsonArray styles = cyclic.value(QStringLiteral("styles")).toArray();
        for (int i = 0; i < styles.size(); ++i) {
            QJsonObject style = styles.at(i).toObject();
            const bool isBody = style.value(QStringLiteral("id")).toString() == sheet.body;
            style.insert(QStringLiteral("parent"), isBody ? sheet.heading : sheet.body);
            styles.replace(i, style);
        }
        cyclic.insert(QStringLiteral("styles"), styles);
        QVERIFY(!RichTextCodec::fromJsonObject(cyclic, target, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("styles"));
        // Pasting styled paragraphs brings their styles once.
        RichTextDocument other;
        other.insertText(other.startPosition(), QStringLiteral("x"));
        const Fragment copied = doc.extractFragment(doc.startPosition(), doc.endPosition());
        QCOMPARE(copied.styles.size(), 2);
        QVERIFY(other.insertFragment(other.endPosition(), copied));
        QVERIFY(other.insertFragment(other.endPosition(), copied));
        QCOMPARE(other.styleSheet().size(), 2);
        int styled = 0;
        for (const Paragraph *p : other.paragraphsInOrder())
            if (p->text == QStringLiteral("Body text") && p->format.styleId == sheet.body) ++styled;
        QCOMPARE(styled, 2);
        QCOMPARE(other.style(sheet.heading)->parentId, sheet.body);
    }

    void styleUpdatesRepaginateLocally()
    {
        RichTextDocument doc;
        buildLines(doc, 45); // 20 lines per page
        const QString quote = doc.createStyle(QStringLiteral("Quote"), QStringLiteral("paragraph"));
        QVERIFY(doc.updateStyle(quote, group("paragraph", {{QStringLiteral("lineHeightPt"), 18}})));
        QVERIFY(doc.applyStyle(quote, ids(doc, 30, 32)));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), 3);
        layout.resetStats();
        QVERIFY(doc.updateStyle(quote, group("paragraph", {{QStringLiteral("indentStartPt"), 24}})));
        QCOMPARE(para(doc, 31)->format.indentStartPt, 24.0);
        QCOMPARE(layout.stats().paragraphsShaped, quint64(3));
        QCOMPARE(layout.stats().lastRestartPage, 1);
        QCOMPARE(layout.pageCount(), 3);
        QCOMPARE(layout.page(1).items.at(10).block, para(doc, 30)->id);
        QCOMPARE(layout.metrics(para(doc, 30)->id)->lines.first().x, 24 * PxPerPt);
    }

    void stylePatchesCopyTheCaretParagraph()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Model\nTarget"));
        ParagraphFormatPatch look;
        look.alignment = Alignment::End;
        look.spaceBeforePt = 10;
        QVERIFY(doc.setParagraphFormat(doc.startPosition(), doc.startPosition(), look));
        CharFormatPatch run;
        run.sizePt = 20;
        QVERIFY(doc.applyCharFormat({para(doc, 0)->id, 0}, {para(doc, 0)->id, 5}, run));
        const QString style = doc.createStyle(QStringLiteral("From model"), QStringLiteral("paragraph"));
        QVERIFY(doc.updateStyle(style, doc.stylePatchFromParagraph(sid(para(doc, 0)->id))));
        QVERIFY(doc.applyStyle(style, ids(doc, 1, 1)));
        QCOMPARE(para(doc, 1)->format.alignment, Alignment::End);
        QCOMPARE(para(doc, 1)->format.spaceBeforePt, 10.0);
        QCOMPARE(effective(doc, para(doc, 1)).sizePt, 20.0);
    }

    void commandsApplyStylesToTheSelection()
    {
        Scene scene;
        QVERIFY(scene.create(true));
        RichTextDocument &doc = scene.document;
        doc.insertText(doc.startPosition(), QStringLiteral("one\ntwo\nthree"));
        const QString quote = doc.createStyle(QStringLiteral("Quote"), QStringLiteral("paragraph"));
        const QString strong = doc.createStyle(QStringLiteral("Strong"), QStringLiteral("character"));
        QVERIFY(doc.updateStyle(strong, group("character", {{QStringLiteral("weight"), 800}})));
        const QVector<Paragraph *> ps = doc.paragraphsInOrder();
        scene.area->setSelection({{ps.at(0)->id, 1}, {ps.at(1)->id, 2}});
        QCOMPARE(scene.area->selectedParagraphIds(), (QStringList{sid(ps.at(0)->id), sid(ps.at(1)->id)}));
        QVERIFY(scene.area->invokeCommand(QStringLiteral("paragraph.style"), {{QStringLiteral("value"), quote}}));
        QCOMPARE(ps.at(0)->format.styleId, quote);
        QCOMPARE(ps.at(1)->format.styleId, quote);
        QCOMPARE(ps.at(2)->format.styleId, QString());
        QCOMPARE(scene.area->commandState(QStringLiteral("paragraph.style")).value(QStringLiteral("value")).toString(), quote);
        QCOMPARE(scene.area->caretContext(), QStringLiteral("Style Quote"));
        QVERIFY(scene.area->hasSelection()); // the selection stays
        QVERIFY(scene.area->invokeCommand(QStringLiteral("text.style"), {{QStringLiteral("value"), strong}}));
        QCOMPARE(doc.charFormat(Spans::formatAt(ps.at(0)->spans, 2)).styleId, strong);
        QCOMPARE(doc.charFormat(Spans::formatAt(ps.at(0)->spans, 0)).styleId, QString());
        QCOMPARE(doc.undoText(), QStringLiteral("Character style"));
        QVERIFY(!scene.area->invokeCommand(QStringLiteral("text.style"), {{QStringLiteral("value"), quote}}));
        QCOMPARE(doc.undoText(), QStringLiteral("Character style"));
    }
};

QTEST_MAIN(RichTextStylesTest)
#include "tst_rich_text_styles.moc"
