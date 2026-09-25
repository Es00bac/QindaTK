// SPDX-License-Identifier: LGPL-3.0-or-later
// Printing (WP-053, D-130/D-131): tickets and page ranges, sheet geometry
// against hand-computed fixtures, PDF output that matches the plan, preview
// pixels equal to job output, snapshot isolation, a bounded preview cache,
// cancellation, device failure with recovery, and page bleed.
#include "print_adapter.h"
#include "rich_text_codec.h"
#include "rich_text_fixtures.h"
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichTextFixtures;

namespace {

struct CaptureLog {
    QVector<QImage> sheets;
    bool aborted = false;
    bool finished = false;
    int failAt = -1;        // beginSheet(index) fails like a paper jam
    bool streaming = false; // closed sheets count as delivered (a streaming printer)
};

// A raster printer: each sheet lands in an image at 96 px per inch.
class CaptureDevice final : public Print::Device {
public:
    explicit CaptureDevice(std::shared_ptr<CaptureLog> log) : m_log(std::move(log)) {}
    bool beginSheet(const Print::Sheet &sheet, int index, QString *error) override
    {
        close();
        if (index == m_log->failAt) {
            *error = QStringLiteral("Paper jam on sheet %1").arg(index + 1);
            return false;
        }
        m_image = QImage(Print::sheetPixelSize(sheet, 1.0), QImage::Format_ARGB32_Premultiplied);
        m_image.fill(Qt::white);
        return m_painter.begin(&m_image);
    }
    QPainter *painter() override { return &m_painter; }
    bool finish(QString *) override
    {
        close();
        m_log->finished = true;
        return true;
    }
    void abort() override
    {
        if (m_painter.isActive()) m_painter.end();
        m_log->aborted = true;
    }
    int deliveredSheets() const override { return m_log->streaming ? int(m_log->sheets.size()) : 0; }

private:
    void close()
    {
        if (!m_painter.isActive()) return;
        m_painter.end();
        m_log->sheets.append(m_image);
    }
    std::shared_ptr<CaptureLog> m_log;
    QImage m_image;
    QPainter m_painter;
};

class CaptureBackend final : public Print::Backend {
public:
    std::shared_ptr<CaptureLog> log = std::make_shared<CaptureLog>();
    QVector<Print::Capabilities> destinations() override
    {
        Print::Capabilities c;
        c.printerId = QStringLiteral("capture");
        c.label = QStringLiteral("Capture printer");
        c.papers = Print::standardPapers();
        c.duplexModes = {QStringLiteral("simplex"), QStringLiteral("long-edge")};
        c.colorModes = {QStringLiteral("color"), QStringLiteral("grayscale")};
        c.maxCopies = 5;
        c.collate = true;
        return {c};
    }
    std::unique_ptr<Print::Device> open(const Print::Ticket &, const Print::Capabilities &, QString *) override
    {
        return std::make_unique<CaptureDevice>(log);
    }
};

struct PdfInfo {
    int pages = 0;
    QVector<QSizeF> boxes;
};

PdfInfo pdfInfo(const QString &path)
{
    QFile file(path);
    PdfInfo info;
    if (!file.open(QIODevice::ReadOnly)) return info;
    const QString text = QString::fromLatin1(file.readAll());
    static const QRegularExpression page(QStringLiteral("/Type\\s*/Page(?![A-Za-z])"));
    static const QRegularExpression box(QStringLiteral(
        "/MediaBox\\s*\\[\\s*([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s*\\]"));
    for (auto it = page.globalMatch(text); it.hasNext(); it.next()) ++info.pages;
    for (auto it = box.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        info.boxes.append(QSizeF(m.captured(3).toDouble() - m.captured(1).toDouble(),
                                 m.captured(4).toDouble() - m.captured(2).toDouble()));
    }
    return info;
}

bool near(double a, double b, double tolerance = 0.01) { return qAbs(a - b) <= tolerance; }

// Three fixture pages (372 x 432 pt): two lines each, page breaks between.
void buildThreePages(RichTextDocument &doc) { buildLines(doc, 6, 2); }

} // namespace

class PrintOutputTest : public QObject {
    Q_OBJECT
private slots:
    void ticketsParseAndValidate()
    {
        const auto ranges = Print::parsePageRanges(QStringLiteral(" 1-3, 5 ,8- "));
        QVERIFY(ranges.has_value());
        QCOMPARE(*ranges, (QVector<Print::PageRange>{{1, 3}, {5, 5}, {8, -1}}));
        QCOMPARE(Print::formatPageRanges(*ranges), QStringLiteral("1-3, 5, 8-"));
        QCOMPARE(Print::selectedPages(*ranges, 9), (QVector<int>{0, 1, 2, 4, 7, 8}));
        QCOMPARE(Print::selectedPages({}, 3), (QVector<int>{0, 1, 2}));
        OperationError error;
        QVERIFY(!Print::parsePageRanges(QStringLiteral("3-1"), &error));
        QCOMPARE(error.fieldPath, QStringLiteral("pages"));
        QVERIFY(!Print::parsePageRanges(QStringLiteral("two")));
        QVERIFY(!Print::ticketFromMap({{QStringLiteral("colorMode"), QStringLiteral("sepia")}}, {}, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("colorMode"));
        QVERIFY(!Print::ticketFromMap({{QStringLiteral("copies"), 1.5}}, {}, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("copies"));

        // PrintPreview's single range and the settings map round-trip.
        const auto ticket = Print::ticketFromMap(
            {{QStringLiteral("pageRange"), QVariantMap{{QStringLiteral("startPage"), 2}, {QStringLiteral("endPage"), 3}}},
             {QStringLiteral("paperId"), QStringLiteral("letter")}}, {});
        QVERIFY(ticket.has_value());
        QCOMPARE(ticket->ranges, (QVector<Print::PageRange>{{2, 3}}));
        const QVariantMap map = Print::ticketToMap(*ticket, 3);
        QCOMPARE(map.value(QStringLiteral("pages")).toString(), QStringLiteral("2-3"));
        QCOMPARE(map.value(QStringLiteral("pageRange")).toMap().value(QStringLiteral("endPage")).toInt(), 3);

        // Out-of-range pages and options the destination lacks are named by field.
        Print::Ticket wide;
        wide.ranges = {{2, 9}};
        wide.duplex = QStringLiteral("long-edge");
        const QVector<ValidationIssue> issues = Print::validateTicket(wide, Print::PdfFileBackend::capabilities(), 3);
        QStringList fields;
        for (const ValidationIssue &issue : issues) fields.append(issue.fieldPath);
        QCOMPARE(fields, (QStringList{QStringLiteral("pages"), QStringLiteral("duplex"), QStringLiteral("outputPath")}));
        QCOMPARE(int(issues.first().code), int(ErrorCode::InvalidArgument));
        QCOMPARE(int(issues.at(1).code), int(ErrorCode::Unsupported));
    }

    void sheetGeometryMatchesFixtures()
    {
        const RichText::PageSetup page = fixturePage();
        const auto paper = [](const QString &id) { return Print::resolvePaper(Print::PdfFileBackend::capabilities(), id); };
        Print::Ticket ticket;
        Print::Sheet sheet = Print::sheetFor(page, 0, ticket, paper(QStringLiteral("document")));
        QCOMPARE(sheet.sizePt, QSizeF(372, 432));
        QCOMPARE(sheet.trimPt, QRectF(0, 0, 372, 432));

        ticket.paperId = QStringLiteral("letter");
        sheet = Print::sheetFor(page, 0, ticket, paper(ticket.paperId));
        QCOMPARE(sheet.sizePt, QSizeF(612, 792));
        QVERIFY(near(sheet.scale, 1.6451612903));
        QVERIFY(near(sheet.trimPt.x(), 0) && near(sheet.trimPt.y(), 40.6451612903));
        QVERIFY(near(sheet.trimPt.width(), 612) && near(sheet.trimPt.height(), 710.7096774194));
        QVERIFY(!sheet.clipped);

        ticket.scaleMode = QStringLiteral("actual");
        sheet = Print::sheetFor(page, 0, ticket, paper(ticket.paperId));
        QCOMPARE(sheet.trimPt, QRectF(120, 180, 372, 432));

        ticket.paperId = QStringLiteral("a4");
        ticket.orientation = QStringLiteral("landscape");
        sheet = Print::sheetFor(page, 0, ticket, paper(ticket.paperId));
        QVERIFY(near(sheet.sizePt.width(), 841.8897637795) && near(sheet.sizePt.height(), 595.2755905512));
        QVERIFY(near(sheet.trimPt.x(), 234.9448818898) && near(sheet.trimPt.y(), 81.6377952756));

        ticket.paperId = QStringLiteral("letter");
        ticket.orientation = QStringLiteral("auto");
        ticket.scaleMode = QStringLiteral("percent");
        ticket.scalePercent = 200;
        QVERIFY(Print::sheetFor(page, 0, ticket, paper(ticket.paperId)).clipped);

        // Bleed and crop marks grow the document-size sheet around the trim box.
        RichText::PageSetup bled = page;
        bled.bleedPt = 9;
        Print::Ticket marks;
        marks.includeBleed = true;
        marks.cropMarks = true;
        sheet = Print::sheetFor(bled, 1, marks, paper(QStringLiteral("document")));
        QCOMPARE(sheet.sizePt, QSizeF(426, 486));
        QCOMPARE(sheet.trimPt, QRectF(27, 27, 372, 432));
        QCOMPARE(sheet.bleedPt, 9.0);
        QCOMPARE(sheet.markPt, Print::CropMarkPt);
        QCOMPARE(sheet.sourcePage, 1);
    }

    void pdfOutputMatchesThePlan()
    {
        RichTextDocument doc;
        buildThreePages(doc);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        QCOMPARE(adapter.pageCount(), 3);
        const QString path = directory.filePath(QStringLiteral("range.pdf"));
        QVERIFY(adapter.applySettings({{QStringLiteral("outputPath"), path}, {QStringLiteral("pages"), QStringLiteral("2-3")},
                                       {QStringLiteral("paperId"), QStringLiteral("letter")}}));
        const QVector<Print::Sheet> plan = adapter.plan();
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan.first().sourcePage, 1);
        const QString id = adapter.print();
        QVERIFY2(!id.isEmpty(), qPrintable(adapter.lastError()));
        QVERIFY(adapter.busy());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(adapter.job().value(QStringLiteral("id")).toString(), id);
        const PdfInfo info = pdfInfo(path);
        QCOMPARE(info.pages, 2);
        QVERIFY(!info.boxes.isEmpty());
        for (const QSizeF &box : info.boxes)
            QVERIFY2(near(box.width(), plan.first().sizePt.width(), 1.0) && near(box.height(), plan.first().sizePt.height(), 1.0),
                     qPrintable(QStringLiteral("%1 x %2").arg(box.width()).arg(box.height())));

        // exportPdf is the default ticket of the same path: document size, every page.
        const QString whole = directory.filePath(QStringLiteral("whole.pdf"));
        QVERIFY2(doc.exportPdf(whole), qPrintable(doc.lastError()));
        const PdfInfo exported = pdfInfo(whole);
        QCOMPARE(exported.pages, 3);
        for (const QSizeF &box : exported.boxes) QVERIFY(near(box.width(), 372, 1.0) && near(box.height(), 432, 1.0));
    }

    void previewPixelsEqualJobOutput()
    {
        RichTextDocument doc;
        buildThreePages(doc);
        auto capture = std::make_shared<CaptureBackend>();
        PrintAdapter adapter;
        adapter.setBackends({capture});
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("capture")},
                                       {QStringLiteral("paperId"), QStringLiteral("a4")},
                                       {QStringLiteral("includeBleed"), false}}));
        QVector<QImage> previews;
        for (int page = 0; page < 3; ++page) previews.append(adapter.previewImage(page, 1.0));
        QVERIFY(!adapter.print().isEmpty());
        // The job prints a snapshot: an edit right after print() does not reach it.
        QVERIFY(doc.insertText(doc.startPosition(), QStringLiteral("Edited after print ")));
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QVERIFY(capture->log->finished);
        QCOMPARE(capture->log->sheets.size(), 3);
        for (int page = 0; page < 3; ++page) {
            QCOMPARE(capture->log->sheets.at(page).size(), Print::sheetPixelSize(adapter.sheetOf(page), 1.0));
            QVERIFY2(capture->log->sheets.at(page) == previews.at(page), qPrintable(QStringLiteral("page %1").arg(page + 1)));
        }
        QVERIFY(adapter.previewImage(0, 1.0) != capture->log->sheets.first());
    }

    void previewCacheIsBounded()
    {
        RichTextDocument doc;
        buildThreePages(doc);
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        adapter.setPreviewCacheLimit(2);
        const QImage first = adapter.previewImage(0, 0.5);
        QVERIFY(!first.isNull());
        adapter.previewImage(1, 0.5);
        adapter.previewImage(2, 0.5);
        QCOMPARE(adapter.previewCacheCount(), 2);
        QCOMPARE(adapter.previewRenderCount(), 3);
        QVERIFY(adapter.previewCacheBytes() <= 2 * first.sizeInBytes());
        adapter.previewImage(2, 0.5);
        QCOMPARE(adapter.previewRenderCount(), 3); // cached
        adapter.previewImage(0, 0.5);
        QCOMPARE(adapter.previewRenderCount(), 4); // evicted earlier, rendered again
        QVERIFY(adapter.previewImage(7, 0.5).isNull());
        const int generation = adapter.previewGeneration();
        QVERIFY(doc.insertText(doc.startPosition(), QStringLiteral("x")));
        QCOMPARE(adapter.previewCacheCount(), 0);
        QVERIFY(adapter.previewGeneration() > generation);

        // Grayscale output is previewed in gray.
        QVERIFY(adapter.applySettings({{QStringLiteral("colorMode"), QStringLiteral("grayscale")}}));
        const QImage gray = adapter.previewImage(0, 0.5);
        bool neutral = true;
        for (int y = 0; y < gray.height() && neutral; ++y)
            for (int x = 0; x < gray.width() && neutral; ++x) {
                const QRgb pixel = gray.pixel(x, y);
                neutral = qRed(pixel) == qGreen(pixel) && qGreen(pixel) == qBlue(pixel);
            }
        QVERIFY(neutral);
    }

    void cancellationDiscardsOutput()
    {
        RichTextDocument doc;
        buildThreePages(doc);
        auto capture = std::make_shared<CaptureBackend>();
        PrintAdapter adapter;
        adapter.setBackends({capture, std::make_shared<Print::PdfFileBackend>()});
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("capture")}}));
        const QString id = adapter.print();
        QVERIFY(!id.isEmpty());
        QVERIFY(adapter.cancel(id));
        QCOMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("cancelling"));
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
        QVERIFY(capture->log->aborted);
        QVERIFY(!capture->log->finished);
        QCOMPARE(adapter.job().value(QStringLiteral("affectedPages")).toString(), QStringLiteral("1-3"));
        QVERIFY(!adapter.busy());
        QVERIFY(!adapter.cancel(id));

        // A cancelled PDF leaves no file behind.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("cancelled.pdf"));
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("pdf")}, {QStringLiteral("outputPath"), path}}));
        const QString pdf = adapter.print();
        QVERIFY(adapter.cancel(pdf));
        QTRY_VERIFY(!adapter.busy());
        QCOMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
        QVERIFY(!QFile::exists(path));
    }

    void deviceFailureKeepsSettingsAndRecovers()
    {
        RichTextDocument doc;
        buildThreePages(doc);
        auto capture = std::make_shared<CaptureBackend>();
        PrintAdapter adapter;
        adapter.setBackends({capture, std::make_shared<Print::PdfFileBackend>()});
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("capture")},
                                       {QStringLiteral("paperId"), QStringLiteral("a4")},
                                       {QStringLiteral("copies"), 2}}));
        capture->log->failAt = 1;
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        const QVariantMap failed = adapter.job();
        QCOMPARE(failed.value(QStringLiteral("failedPage")).toInt(), 2);
        QCOMPARE(failed.value(QStringLiteral("affectedPages")).toString(), QStringLiteral("1-3"));
        QCOMPARE(failed.value(QStringLiteral("affectedStart")).toInt(), 1);
        QCOMPARE(failed.value(QStringLiteral("affectedEnd")).toInt(), 3);
        QVERIFY(failed.value(QStringLiteral("error")).toString().contains(QStringLiteral("Paper jam")));
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("backendFailure"));
        QVERIFY(capture->log->aborted);
        QCOMPARE(adapter.settings().value(QStringLiteral("paperId")).toString(), QStringLiteral("a4"));
        QCOMPARE(adapter.settings().value(QStringLiteral("copies")).toInt(), 2);

        // A streaming printer already delivered sheet 1: only pages 2-3 are affected.
        capture->log->sheets.clear();
        capture->log->streaming = true;
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(adapter.job().value(QStringLiteral("affectedPages")).toString(), QStringLiteral("2-3"));

        // The fault clears and the same ticket prints.
        capture->log->failAt = -1;
        capture->log->sheets.clear();
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(capture->log->sheets.size(), 3);

        // A missing folder is refused before a job starts; a read-only one
        // fails on the first sheet without a file; a good path then works.
        QTemporaryDir directory;
        const QString missing = directory.filePath(QStringLiteral("missing-folder/out.pdf"));
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), QStringLiteral("pdf")}, {QStringLiteral("outputPath"), missing},
                                       {QStringLiteral("copies"), 1}}));
        QVERIFY(adapter.print().isEmpty());
        QVERIFY(adapter.lastError().contains(QStringLiteral("does not exist")));
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("locked")));
        const QString locked = directory.filePath(QStringLiteral("locked"));
        QVERIFY(QFile::setPermissions(locked, QFile::ReadOwner | QFile::ExeOwner));
        const QString bad = directory.filePath(QStringLiteral("locked/out.pdf"));
        QVERIFY(adapter.applySettings({{QStringLiteral("outputPath"), bad}}));
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(adapter.job().value(QStringLiteral("failedPage")).toInt(), 1);
        QVERIFY(!QFile::exists(bad));
        QVERIFY(QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString good = directory.filePath(QStringLiteral("out.pdf"));
        QVERIFY(adapter.applySettings({{QStringLiteral("outputPath"), good}}));
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(pdfInfo(good).pages, 3);
    }

    void refusedTicketsStartNoJob()
    {
        RichTextDocument doc;
        buildThreePages(doc);
        QTemporaryDir directory;
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("outputPath"), directory.filePath(QStringLiteral("x.pdf"))},
                                       {QStringLiteral("pages"), QStringLiteral("2-9")}}));
        QVERIFY(adapter.print().isEmpty());
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("invalidArgument"));
        QVERIFY(!adapter.busy());
        QCOMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("idle"));
        QCOMPARE(adapter.sheetCount(), 2); // pages 2-3 exist and are previewed

        QVERIFY(!adapter.applySettings({{QStringLiteral("pages"), QStringLiteral("x")}}));
        QCOMPARE(adapter.settings().value(QStringLiteral("pages")).toString(), QStringLiteral("2-9"));

        QVERIFY(adapter.applySettings({{QStringLiteral("pages"), QString()}, {QStringLiteral("printerId"), QStringLiteral("printer:none")}}));
        QVERIFY(adapter.print().isEmpty());
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("unavailable"));
        QCOMPARE(adapter.capabilities().value(QStringLiteral("available")).toBool(), false);
    }

    void bleedIsPageSetup()
    {
        RichTextDocument doc;
        QVariantMap settings = doc.pageSetupMm();
        QCOMPARE(settings.value(QStringLiteral("bleedMm")).toDouble(), 0.0);
        settings.insert(QStringLiteral("bleedMm"), 3.0);
        QVERIFY2(doc.applyPageSetupMm(settings), qPrintable(doc.lastError()));
        QVERIFY(near(doc.pageSetupValue().bleedPt, 8.5039370079, 1e-6));
        QVERIFY(near(doc.pageSetupMm().value(QStringLiteral("bleedMm")).toDouble(), 3.0, 1e-9));
        QVERIFY(near(doc.pageSetupMap().value(QStringLiteral("bleedPt")).toDouble(), 8.5039370079, 1e-6));
        QCOMPARE(RichTextCodec::requiredVersion(doc), 2);
        RichTextDocument reopened;
        QVERIFY(reopened.loadJson(doc.toJson()));
        QCOMPARE(reopened.pageSetupValue().bleedPt, doc.pageSetupValue().bleedPt);
        settings.insert(QStringLiteral("scope"), QStringLiteral("section"));
        QVERIFY(!doc.applyPageSetupMm(settings));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(doc.undo());
        QCOMPARE(doc.pageSetupValue().bleedPt, 0.0);
        QCOMPARE(RichTextCodec::requiredVersion(doc), 1);
    }
};

QTEST_MAIN(PrintOutputTest)
#include "tst_print_output.moc"
