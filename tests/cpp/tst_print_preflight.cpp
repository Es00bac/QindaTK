// SPDX-License-Identifier: LGPL-3.0-or-later
// Publishing preflight (WP-053, D-132): known fixtures for missing fonts,
// missing and low-resolution images, overset frames, WP-052 fields,
// equations and review marks, and output options produce actionable issues
// with stable IDs, and each validated repair clears its issue.
#include "print_adapter.h"
#include "rich_text_fixtures.h"
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }
QString pid(const RichTextDocument &doc, int index) { return sid(doc.paragraphsInOrder().at(index)->id); }

QVariantMap findIssue(PrintAdapter &adapter, const QString &id)
{
    for (const QVariant &value : adapter.runPreflight())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

QStringList issueIds(PrintAdapter &adapter)
{
    QStringList ids;
    for (const QVariant &value : adapter.runPreflight()) ids.append(value.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

QString writeImage(const QTemporaryDir &directory, const QString &name, int pixels)
{
    QImage image(pixels, pixels, QImage::Format_RGB32);
    image.fill(QColor(QStringLiteral("#3070b0")));
    const QString path = directory.filePath(name);
    return image.save(path, "PNG") ? path : QString();
}

QString insertImage(RichTextDocument &doc, const QString &source)
{
    RichObject object;
    object.kind = QStringLiteral("image");
    object.widthPt = 72;
    object.heightPt = 72;
    object.source = source;
    object.altText = QStringLiteral("Chart");
    const Paragraph *first = doc.paragraphsInOrder().first();
    if (!doc.insertObject({first->id, 0}, object)) return {};
    for (const RichObject &candidate : doc.objects())
        if (candidate.kind == QLatin1String("image") && candidate.source == source) return candidate.id;
    return {};
}

// A printer that cannot mark the outer 48 pt of the sheet.
class EdgeBackend final : public Print::Backend {
public:
    QVector<Print::Capabilities> destinations() override
    {
        Print::Capabilities c;
        c.printerId = QStringLiteral("edge");
        c.label = QStringLiteral("Edge printer");
        c.papers = Print::standardPapers();
        c.printableMarginsPt = QMarginsF(48, 48, 48, 48);
        return {c};
    }
    std::unique_ptr<Print::Device> open(const Print::Ticket &, const Print::Capabilities &, QString *error) override
    {
        *error = QStringLiteral("not used");
        return nullptr;
    }
};

} // namespace

class PrintPreflightTest : public QObject {
    Q_OBJECT
private slots:
    void missingFontIsReportedAndReplaced()
    {
        RichTextDocument doc;
        buildLines(doc, 3);
        CharFormatPatch missing;
        missing.family = QStringLiteral("QindaTK Missing Face");
        QVERIFY(doc.applyCharFormat(doc.startPosition(), doc.endPosition(), missing));
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        const QString id = QStringLiteral("font-missing:QindaTK Missing Face");
        QVariantMap issue = findIssue(adapter, id);
        QCOMPARE(issue.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
        QCOMPARE(issue.value(QStringLiteral("category")).toString(), QStringLiteral("Fonts"));
        QCOMPARE(issue.value(QStringLiteral("targetId")).toString(), QStringLiteral("font:QindaTK Missing Face"));
        QCOMPARE(issue.value(QStringLiteral("paragraphId")).toString(), pid(doc, 0));
        QCOMPARE(issue.value(QStringLiteral("pageIndex")).toInt(), 0);
        QCOMPARE(issue.value(QStringLiteral("repairActionIds")).toStringList(), QStringList{QStringLiteral("replace-font")});
        QCOMPARE(issue.value(QStringLiteral("repairLabel")).toString(), QStringLiteral("Use sans-serif"));
        QVERIFY(!adapter.preflightStale());

        // The press profile makes a substitution block output.
        adapter.setProfileId(QStringLiteral("press"));
        QCOMPARE(findIssue(adapter, id).value(QStringLiteral("severity")).toString(), QStringLiteral("error"));
        QVERIFY(adapter.blockingIssueCount() >= 1);
        adapter.setProfileId(QStringLiteral("print"));

        QVERIFY(!adapter.repair(id, QStringLiteral("update-fields")));
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY2(adapter.repair(id, QStringLiteral("replace-font")), qPrintable(adapter.lastError()));
        QVERIFY(doc.missingFontFamilies().isEmpty());
        QVERIFY(!issueIds(adapter).contains(id));
        // One undo step brings the font back, and the issue with it.
        QVERIFY(doc.undo());
        QCOMPARE(doc.missingFontFamilies(), QStringList{QStringLiteral("QindaTK Missing Face")});
        QVERIFY(issueIds(adapter).contains(id));
        QVERIFY(!adapter.repair(QStringLiteral("font-missing:Other"), QStringLiteral("replace-font")));
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("invalidArgument"));
    }

    void imagesAreCheckedAndRelinked()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        RichTextDocument doc;
        buildLines(doc, 3);
        const QString missing = insertImage(doc, QStringLiteral("file:///nonexistent-qindatk/chart.png"));
        QVERIFY(!missing.isEmpty());
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        const QString id = QStringLiteral("image-missing:") + missing;
        const QVariantMap issue = findIssue(adapter, id);
        QCOMPARE(issue.value(QStringLiteral("severity")).toString(), QStringLiteral("error"));
        QCOMPARE(issue.value(QStringLiteral("targetId")).toString(), QStringLiteral("object:") + missing);
        QCOMPARE(issue.value(QStringLiteral("paragraphId")).toString(), pid(doc, 0));
        QCOMPARE(issue.value(QStringLiteral("repairActionIds")).toStringList(), QStringList{QStringLiteral("relink-image")});

        // A file that is not an image is refused and nothing changes.
        const QString text = directory.filePath(QStringLiteral("notes.png"));
        QFile file(text);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not an image");
        file.close();
        const quint64 revision = doc.revision();
        QVERIFY(!adapter.repair(id, QStringLiteral("relink-image"), text));
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("validation"));
        QCOMPARE(doc.revision(), revision);

        // A 50 px image over one inch prints at 50 dpi: linked, then low resolution.
        const QString small = writeImage(directory, QStringLiteral("small.png"), 50);
        QVERIFY(adapter.repair(id, QStringLiteral("relink-image"), small));
        QCOMPARE(doc.object(missing)->source, QUrl::fromLocalFile(small).toString());
        const QStringList ids = issueIds(adapter);
        QVERIFY(!ids.contains(id));
        const QString resolution = QStringLiteral("image-resolution:") + missing;
        QVERIFY(ids.contains(resolution));
        QVERIFY(findIssue(adapter, resolution).value(QStringLiteral("description")).toString().contains(QStringLiteral("50 dpi")));
        // A 300 px image at the same size meets both profiles.
        RichObject sharp = *doc.object(missing);
        sharp.source = QUrl::fromLocalFile(writeImage(directory, QStringLiteral("sharp.png"), 300)).toString();
        QVERIFY(doc.setObject(sharp));
        adapter.setProfileId(QStringLiteral("press"));
        QVERIFY(!issueIds(adapter).contains(resolution));

        // Asset-library sources are named, not silently drawn as placeholders.
        const QString asset = insertImage(doc, QStringLiteral("asset:library/logo"));
        QVERIFY(issueIds(adapter).contains(QStringLiteral("image-unresolved:") + asset));
    }

    void oversetFramesClearAfterResize()
    {
        RichTextDocument doc;
        buildLines(doc, 12);
        TextFrame frame;
        frame.id = QStringLiteral("A");
        frame.page = 0;
        frame.xPt = 36;
        frame.yPt = 36;
        frame.widthPt = 300;
        frame.heightPt = 120; // six of the twelve 18 pt lines
        FrameSet frames;
        frames.frames = {frame};
        frames.storyStart = QStringLiteral("A");
        QVERIFY(doc.setFrameSet(frames, QStringLiteral("Frames")));
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        const QVariantMap issue = findIssue(adapter, QStringLiteral("text-overset:A"));
        QCOMPARE(issue.value(QStringLiteral("severity")).toString(), QStringLiteral("error"));
        QCOMPARE(issue.value(QStringLiteral("targetId")).toString(), QStringLiteral("frame:A"));
        QCOMPARE(issue.value(QStringLiteral("pageIndex")).toInt(), 0);
        QVERIFY(doc.setTextFrameGeometry(QStringLiteral("A"), {{QStringLiteral("heightMm"), 300 * 25.4 / 72}}));
        QVERIFY(adapter.preflightStale());
        QVERIFY(!issueIds(adapter).contains(QStringLiteral("text-overset:A")));
    }

    void fieldsAndEquationsAreChecked()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Intro\n: alpha\nSee "));
        const QString figure = doc.insertReference(pid(doc, 1), 0, QStringLiteral("caption"), QStringLiteral("Figure"));
        const QString xref = doc.insertReference(pid(doc, 2), 4, QStringLiteral("cross-reference"), figure);
        QVERIFY(!figure.isEmpty() && !xref.isEmpty());
        PrintAdapter adapter;

        // A result edited outside the toolkit is stale until the fields update.
        RichTextDocument edited;
        QVERIFY(edited.loadJson(doc.toJson().replace(QStringLiteral("Figure 1: alpha"), QStringLiteral("Figure 9: alpha"))));
        adapter.setDocument(&edited);
        const QVariantMap stale = findIssue(adapter, QStringLiteral("fields-stale"));
        QCOMPARE(stale.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
        QCOMPARE(stale.value(QStringLiteral("targetId")).toString(), QStringLiteral("field:") + figure);
        QVERIFY2(adapter.repair(QStringLiteral("fields-stale"), QStringLiteral("update-fields")), qPrintable(adapter.lastError()));
        QCOMPARE(edited.paragraphsInOrder().at(1)->text, QStringLiteral("Figure 1: alpha"));

        // Deleting the caption breaks the cross-reference.
        adapter.setDocument(&doc);
        const Paragraph *caption = doc.paragraphsInOrder().at(1);
        QVERIFY(doc.deleteRange({doc.paragraphsInOrder().at(0)->id, 5}, {caption->id, int(caption->text.size())}));
        const QVariantMap broken = findIssue(adapter, QStringLiteral("field-broken:") + xref);
        QCOMPARE(broken.value(QStringLiteral("severity")).toString(), QStringLiteral("error"));
        QCOMPARE(broken.value(QStringLiteral("targetId")).toString(), QStringLiteral("field:") + xref);
        QVERIFY(!broken.value(QStringLiteral("paragraphId")).toString().isEmpty());
        QVERIFY(doc.undo());
        QVERIFY(!issueIds(adapter).contains(QStringLiteral("field-broken:") + xref));

        // An equation whose source cannot be typeset (loaded from a file).
        RichObject equation;
        equation.kind = QStringLiteral("equation");
        equation.source = QStringLiteral("\\frac{a}");
        equation.widthPt = 24;
        equation.heightPt = 12;
        QVERIFY(doc.insertObject({doc.paragraphsInOrder().at(0)->id, 0}, equation));
        QString equationId;
        for (const RichObject &object : doc.objects())
            if (object.kind == QLatin1String("equation")) equationId = object.id;
        const QVariantMap invalid = findIssue(adapter, QStringLiteral("equation-invalid:") + equationId);
        QCOMPARE(invalid.value(QStringLiteral("severity")).toString(), QStringLiteral("error"));
        QCOMPARE(invalid.value(QStringLiteral("targetId")).toString(), QStringLiteral("equation:") + equationId);
        QVERIFY(doc.setEquationSource(equationId, QStringLiteral("\\frac{a}{b}")));
        QVERIFY(!issueIds(adapter).contains(QStringLiteral("equation-invalid:") + equationId));
    }

    void openReviewMarksAreReported()
    {
        RichTextDocument doc;
        buildLines(doc, 2);
        doc.setTrackChanges(true);
        doc.setReviewAuthor(QStringLiteral("Ada"));
        QVERIFY(doc.insertText(doc.paragraphEnd(doc.paragraphsInOrder().at(1)->id), QStringLiteral(" added")));
        const QString thread = doc.addComment(pid(doc, 0), 0, pid(doc, 0), 2, QStringLiteral("Check this"));
        QVERIFY(!thread.isEmpty());
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        const QVariantMap issue = findIssue(adapter, QStringLiteral("review-markup"));
        QCOMPARE(issue.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
        QVERIFY(issue.value(QStringLiteral("description")).toString().startsWith(QStringLiteral("1 tracked change")));
        QVERIFY(issue.value(QStringLiteral("description")).toString().contains(QStringLiteral("1 open comment")));
        adapter.setProfileId(QStringLiteral("press"));
        QCOMPARE(findIssue(adapter, QStringLiteral("review-markup")).value(QStringLiteral("severity")).toString(),
                 QStringLiteral("error"));
        QString change;
        for (const QVariant &item : doc.reviewItems())
            if (item.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("change"))
                change = item.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(doc.acceptChange(change));
        QVERIFY(doc.resolveComment(thread));
        QVERIFY(!issueIds(adapter).contains(QStringLiteral("review-markup")));
    }

    void outputOptionsAreRepaired()
    {
        QTemporaryDir directory;
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        PrintAdapter adapter;
        adapter.setBackends({std::make_shared<Print::PdfFileBackend>(), std::make_shared<EdgeBackend>()});
        adapter.setDocument(&doc);
        QStringList ids = issueIds(adapter);
        QVERIFY(ids.contains(QStringLiteral("output-outputPath")));
        const QString path = directory.filePath(QStringLiteral("out.pdf"));
        QVERIFY(!adapter.repair(QStringLiteral("output-outputPath"), QStringLiteral("choose-output-file"), QString()));
        QVERIFY(adapter.repair(QStringLiteral("output-outputPath"), QStringLiteral("choose-output-file"), path));
        QCOMPARE(adapter.settings().value(QStringLiteral("outputPath")).toString(), path);

        QVERIFY(adapter.applySettings({{QStringLiteral("duplex"), QStringLiteral("long-edge")},
                                       {QStringLiteral("pages"), QStringLiteral("2-7")},
                                       {QStringLiteral("includeBleed"), true},
                                       {QStringLiteral("paperId"), QStringLiteral("letter")},
                                       {QStringLiteral("scaleMode"), QStringLiteral("percent")},
                                       {QStringLiteral("scalePercent"), 200}}));
        const QVariantMap duplex = findIssue(adapter, QStringLiteral("output-duplex"));
        QCOMPARE(duplex.value(QStringLiteral("severity")).toString(), QStringLiteral("error"));
        QCOMPARE(duplex.value(QStringLiteral("repairLabel")).toString(), QStringLiteral("Use Single-sided"));
        QCOMPARE(duplex.value(QStringLiteral("targetId")).toString(), QStringLiteral("setting:duplex"));
        QVERIFY(adapter.print().isEmpty()); // refused while the ticket asks for the impossible
        QVERIFY(adapter.repair(QStringLiteral("output-duplex"), QStringLiteral("use-supported")));
        QCOMPARE(adapter.settings().value(QStringLiteral("duplex")).toString(), QStringLiteral("simplex"));
        QVERIFY(adapter.repair(QStringLiteral("output-pages"), QStringLiteral("print-all-pages")));
        QCOMPARE(adapter.settings().value(QStringLiteral("pages")).toString(), QString());
        QVERIFY(issueIds(adapter).contains(QStringLiteral("output-clipped")));
        QVERIFY(adapter.repair(QStringLiteral("output-clipped"), QStringLiteral("fit-to-paper")));
        QCOMPARE(adapter.settings().value(QStringLiteral("scaleMode")).toString(), QStringLiteral("fit"));
        QVERIFY(adapter.repair(QStringLiteral("output-bleed-missing"), QStringLiteral("disable-bleed")));
        ids = issueIds(adapter);
        QVERIFY2(ids.isEmpty(), qPrintable(ids.join(u',')));
        QCOMPARE(adapter.blockingIssueCount(), 0);

        // A printer's unprintable edge overlapping the text is a warning.
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("edge")},
                                       {QStringLiteral("paperId"), QStringLiteral("document")},
                                       {QStringLiteral("scaleMode"), QStringLiteral("actual")}}));
        QCOMPARE(findIssue(adapter, QStringLiteral("output-unprintable-edge")).value(QStringLiteral("severity")).toString(),
                 QStringLiteral("warning"));

        // An unknown destination blocks output and has no automatic repair.
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("printer:gone")}}));
        const QVariantMap gone = findIssue(adapter, QStringLiteral("output-printerId"));
        QCOMPARE(gone.value(QStringLiteral("severity")).toString(), QStringLiteral("blocker"));
        QVERIFY(gone.value(QStringLiteral("repairActionIds")).toStringList().isEmpty());
        QVERIFY(adapter.blockingIssueCount() >= 1);
    }
};

QTEST_MAIN(PrintPreflightTest)
#include "tst_print_preflight.moc"
