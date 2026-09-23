// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_codec.h"
#include "rich_text_spans.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;

namespace {

// Mixed scripts, every block type, list, link, three object placements.
void buildComplexDocument(RichTextDocument &doc)
{
    PageSetup page;
    page.widthPt = 612;
    page.heightPt = 792;
    page.marginLeftPt = page.marginRightPt = 54;
    QVERIFY(doc.setPageSetup(page));
    doc.insertText(doc.startPosition(), QStringLiteral(
        "Mixed العربية עברית "
        "क्ष 中文 \U0001F469‍\U0001F469‍\U0001F467 é\n"
        "Second line with a link\nItem one\nItem two"));
    const auto ps = doc.paragraphsInOrder();
    CharFormatPatch bold;
    bold.weight = 700;
    bold.color = QColor(0x20, 0x40, 0x80);
    QVERIFY(doc.applyCharFormat({ps.at(0)->id, 0}, {ps.at(0)->id, 5}, bold));
    CharFormatPatch link;
    link.href = QStringLiteral("https://example.org/a?b=c");
    link.underline = 1;
    QVERIFY(doc.applyCharFormat({ps.at(1)->id, 19}, {ps.at(1)->id, 23}, link));
    ParagraphFormatPatch heading;
    heading.headingLevel = 1;
    heading.alignment = Alignment::Center;
    heading.direction = Direction::Auto;
    heading.keepWithNext = true;
    QVERIFY(doc.setParagraphFormat({ps.at(0)->id, 0}, {ps.at(0)->id, 0}, heading));
    QVERIFY(doc.setList({ps.at(2)->id, 0}, {ps.at(3)->id, 0}, ListKind::Decimal));
    RichObject inlineObject;
    inlineObject.widthPt = 24;
    inlineObject.heightPt = 12;
    inlineObject.source = QStringLiteral("asset:icons/star");
    RichObject floatObject = inlineObject;
    floatObject.placement = Placement::FloatStart;
    floatObject.heightPt = 120;
    floatObject.altText = QStringLiteral("Portrait");
    RichObject pageObject = inlineObject;
    pageObject.placement = Placement::Page;
    pageObject.offsetXPt = 300;
    pageObject.offsetYPt = 10;
    QVERIFY(doc.insertObject({ps.at(1)->id, 0}, inlineObject));
    QVERIFY(doc.insertObject({ps.at(1)->id, 3}, floatObject));
    QVERIFY(doc.insertObject({ps.at(2)->id, 0}, pageObject));
    const auto cell = doc.insertTable(doc.endPosition(), 3, 2, 1);
    QVERIFY(cell);
    doc.insertText(*cell, QStringLiteral("שלום cell"));
    QVERIFY(doc.insertPageBreak(doc.endPosition()));
    doc.insertText(doc.endPosition(), QStringLiteral("After the break"));
}

} // namespace

class RichTextCodecTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripIsCanonicalAndEquivalent()
    {
        RichTextDocument doc;
        buildComplexDocument(doc);
        const QByteArray first = RichTextCodec::encode(doc);
        RichTextDocument reopened;
        OperationError error;
        QStringList warnings;
        QVERIFY2(RichTextCodec::decode(first, reopened, &error, &warnings), qPrintable(error.message));
        QVERIFY(warnings.isEmpty());
        QCOMPARE(RichTextCodec::encode(reopened), first);
        QCOMPARE(reopened.documentId(), doc.documentId());
        QCOMPARE(reopened.paragraphIds(), doc.paragraphIds());
        QCOMPARE(reopened.text(), doc.text());
        QCOMPARE(reopened.objects().size(), 3);
        QCOMPARE(reopened.pageSetupValue(), doc.pageSetupValue());
        QCOMPARE(reopened.sectionIds(), doc.sectionIds());
        const auto a = doc.paragraphsInOrder();
        const auto b = reopened.paragraphsInOrder();
        for (int i = 0; i < a.size(); ++i) {
            QCOMPARE(b.at(i)->format, a.at(i)->format);
            QCOMPARE(b.at(i)->spans.size(), a.at(i)->spans.size());
            for (int s = 0; s < a.at(i)->spans.size(); ++s)
                QCOMPARE(reopened.charFormat(b.at(i)->spans.at(s).format),
                         doc.charFormat(a.at(i)->spans.at(s).format));
        }
        QVERIFY(!reopened.canUndo());
    }

    void fileRoundTripThroughAtomicSave()
    {
        QTemporaryDir dir;
        RichTextDocument doc;
        buildComplexDocument(doc);
        const QString path = dir.filePath(QStringLiteral("fixture.qtkrich"));
        QVERIFY(doc.saveToFile(path));
        RichTextDocument reopened;
        QVERIFY(reopened.loadFromFile(path));
        QCOMPARE(reopened.toJson(), doc.toJson());
    }

    void rejectsInvalidInputWithoutChangingTheDocument_data()
    {
        QTest::addColumn<QString>("mutation");
        QTest::addColumn<QString>("fieldPath");
        QTest::newRow("format table") << "formats0" << "formats";
        QTest::newRow("span coverage") << "spans" << "blocks[0].paragraph.spans";
        QTest::newRow("object anchor") << "anchor" << "blocks[0].paragraph.text";
        QTest::newRow("unknown list") << "list" << "blocks[0].paragraph.format.list";
        QTest::newRow("lone surrogate") << "surrogate" << "blocks[0].paragraph.text";
        QTest::newRow("duplicate id") << "duplicate" << "blocks[1].paragraph.id";
        QTest::newRow("cell count") << "cells" << "blocks[1].table.rows[0].cells";
        QTest::newRow("table last") << "tableLast" << "blocks";
        QTest::newRow("schema") << "schema" << "schema";
    }

    void rejectsInvalidInputWithoutChangingTheDocument()
    {
        QFETCH(QString, mutation);
        QFETCH(QString, fieldPath);
        RichTextDocument source;
        source.insertText(source.startPosition(), QStringLiteral("alpha\nbeta"));
        QVERIFY(source.insertTable(source.endPosition(), 1, 2));
        QJsonObject root = RichTextCodec::toJsonObject(source);
        QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
        auto editParagraph = [&](int index, auto edit) {
            QJsonObject block = blocks.at(index).toObject();
            QJsonObject p = block.value(QStringLiteral("paragraph")).toObject();
            edit(p);
            block.insert(QStringLiteral("paragraph"), p);
            blocks.replace(index, block);
        };
        if (mutation == u"formats0") {
            root.insert(QStringLiteral("formats"), QJsonArray{QJsonObject{{QStringLiteral("weight"), 700}}});
        } else if (mutation == u"spans") {
            editParagraph(0, [](QJsonObject &p) { p.insert(QStringLiteral("spans"), QJsonArray{QJsonArray{2, 0}}); });
        } else if (mutation == u"anchor") {
            editParagraph(0, [](QJsonObject &p) { p.insert(QStringLiteral("text"), QStringLiteral("al￼ha")); });
        } else if (mutation == u"list") {
            editParagraph(0, [](QJsonObject &p) { p.insert(QStringLiteral("format"), QJsonObject{{QStringLiteral("list"), QStringLiteral("nope")}}); });
        } else if (mutation == u"surrogate") {
            editParagraph(0, [](QJsonObject &p) { p.insert(QStringLiteral("text"), QString(QChar(0xD83D)) + QStringLiteral("lpha")); });
        } else if (mutation == u"duplicate") {
            const QString id = blocks.at(0).toObject().value(QStringLiteral("paragraph")).toObject().value(QStringLiteral("id")).toString();
            editParagraph(1, [&id](QJsonObject &p) { p.insert(QStringLiteral("id"), id); });
        } else if (mutation == u"cells") {
            QJsonObject block = blocks.at(2).toObject();
            QJsonObject table = block.value(QStringLiteral("table")).toObject();
            table.insert(QStringLiteral("columns"), QJsonArray{1, 1, 1});
            block.insert(QStringLiteral("table"), table);
            blocks.replace(2, block);
            blocks.removeAt(1);
        } else if (mutation == u"tableLast") {
            blocks.removeLast();
        } else if (mutation == u"schema") {
            root.insert(QStringLiteral("schema"), QStringLiteral("qindatk.other"));
        }
        root.insert(QStringLiteral("blocks"), blocks);
        RichTextDocument target;
        target.insertText(target.startPosition(), QStringLiteral("unchanged"));
        const QString before = target.toJson();
        const QString revision = target.revisionString();
        OperationError error;
        QVERIFY(!RichTextCodec::decode(QJsonDocument(root).toJson(), target, &error));
        QVERIFY(error);
        QCOMPARE(error.fieldPath, fieldPath);
        QCOMPARE(target.toJson(), before);
        QCOMPARE(target.revisionString(), revision);
        QVERIFY(target.canUndo());
    }

    void unknownMembersAreReportedNotSilentlyDropped()
    {
        RichTextDocument source;
        source.insertText(source.startPosition(), QStringLiteral("x"));
        QJsonObject root = RichTextCodec::toJsonObject(source);
        root.insert(QStringLiteral("futureFeature"), true);
        RichTextDocument target;
        QStringList warnings;
        QVERIFY(RichTextCodec::decode(QJsonDocument(root).toJson(), target, nullptr, &warnings));
        QCOMPARE(warnings, QStringList{QStringLiteral("root.futureFeature ignored")});
    }

    void fragmentsCopyAndPasteWithFreshIdentities()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Hello bold world\nsecond"));
        auto ps = doc.paragraphsInOrder();
        CharFormatPatch bold;
        bold.weight = 700;
        QVERIFY(doc.applyCharFormat({ps.at(0)->id, 6}, {ps.at(0)->id, 10}, bold));
        RichObject star;
        star.widthPt = 10;
        star.heightPt = 10;
        QVERIFY(doc.insertObject({ps.at(1)->id, 2}, star));
        const Fragment fragment = doc.extractFragment({ps.at(0)->id, 6}, {ps.at(1)->id, 4});
        QCOMPARE(fragment.blocks.size(), 2);
        QVERIFY(fragment.openStart && fragment.openEnd);
        // Through the clipboard JSON and back.
        OperationError error;
        const auto parsed = RichTextCodec::fragmentFromJson(RichTextCodec::fragmentToJson(fragment), &error);
        QVERIFY2(parsed, qPrintable(error.message));
        const int undoBefore = doc.canUndo();
        const auto caret = doc.insertFragment(doc.endPosition(), *parsed);
        QVERIFY(caret);
        ps = doc.paragraphsInOrder();
        QCOMPARE(ps.size(), 3);
        QCOMPARE(ps.at(1)->text, QStringLiteral("se￼condbold world"));
        QCOMPARE(ps.at(2)->text, QStringLiteral("se￼c"));
        QCOMPARE(*caret, (Position{ps.at(2)->id, 4}));
        QCOMPARE(doc.objects().size(), 2);
        QCOMPARE(doc.charFormat(Spans::formatAt(ps.at(1)->spans, 8)).weight, 700);
        QVERIFY(ps.at(2)->id != ps.at(1)->id);
        QVERIFY(undoBefore);
        QVERIFY(doc.undo());
        QCOMPARE(doc.text(), QStringLiteral("Hello bold world\nsecond"));
        QCOMPARE(doc.objects().size(), 1);
    }

    void pasteInsideASingleParagraphKeepsRuns()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("abcdef"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        CharFormatPatch italic;
        italic.italic = 1;
        QVERIFY(doc.applyCharFormat({p->id, 1}, {p->id, 3}, italic));
        const Fragment fragment = doc.extractFragment({p->id, 0}, {p->id, 3});
        QVERIFY(doc.insertFragment({p->id, 6}, fragment));
        QCOMPARE(p->text, QStringLiteral("abcdefabc"));
        QCOMPARE(doc.charFormat(Spans::formatAt(p->spans, 7)).italic, qint8(1));
        QCOMPARE(doc.charFormat(Spans::formatAt(p->spans, 6)).italic, qint8(-1));
    }
};

QTEST_GUILESS_MAIN(RichTextCodecTest)
#include "tst_rich_text_codec.moc"
