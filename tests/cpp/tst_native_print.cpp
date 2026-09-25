// SPDX-License-Identifier: LGPL-3.0-or-later
// The native print backend (WP-053, PLT-04): system printers as
// destinations, the system print engine writing a file, cancellation without
// output, a vanished printer refused while the adapter keeps working, jobs
// that count as printed only when the spooler accepts them, the job options a
// printer receives, and file output that replaces a target atomically or not
// at all. Real printer runs are qualification work (WP-088); CUPS may list no
// printers here, so a test spooler stands in for the queue.
#include "native_print_backend.h"
#include "print_adapter.h"
#include "rich_text_fixtures.h"
#include <QApplication>
#include <QPrinterInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichTextFixtures;

namespace {

// A queue that records what it is given and accepts or refuses on demand.
class FakeSpooler final : public PrintSpooler {
public:
    struct Received { Job job; QByteArray pdf; };
    bool accept = true;
    QString refusalReason;
    QVector<Received> received;
    QVector<Print::Capabilities> printers() override
    {
        Print::Capabilities c;
        c.printerId = NativePrintBackend::printerId(QStringLiteral("ghost"));
        c.label = QStringLiteral("Ghost printer");
        c.kind = QStringLiteral("printer");
        c.papers = Print::standardPapers();
        c.duplexModes = {QStringLiteral("simplex"), QStringLiteral("long-edge"), QStringLiteral("short-edge")};
        c.colorModes = {QStringLiteral("color"), QStringLiteral("grayscale")};
        c.maxCopies = 99;
        c.collate = true;
        return {c};
    }
    QString refusal(const QString &printerName) override
    {
        return printerName == QLatin1String("ghost") ? refusalReason : QStringLiteral("%1 is gone").arg(printerName);
    }
    int submit(const Job &job, QString *error) override
    {
        QFile file(job.pdfPath);
        received.append({job, file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray()});
        if (accept) return 42;
        *error = QStringLiteral("client-error-not-accepting-jobs");
        return 0;
    }
};

QStringList entries(const QString &directory)
{
    return QDir(directory).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()) : QString();
}

bool writeText(const QString &path, const QByteArray &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

int pdfPages(const QString &path, QSizeF *firstBox)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    const QString text = QString::fromLatin1(file.readAll());
    static const QRegularExpression page(QStringLiteral("/Type\\s*/Page(?![A-Za-z])"));
    static const QRegularExpression box(QStringLiteral(
        "/MediaBox\\s*\\[\\s*([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s*\\]"));
    const QRegularExpressionMatch m = box.match(text);
    if (m.hasMatch() && firstBox)
        *firstBox = QSizeF(m.captured(3).toDouble() - m.captured(1).toDouble(),
                           m.captured(4).toDouble() - m.captured(2).toDouble());
    int pages = 0;
    for (auto it = page.globalMatch(text); it.hasNext(); it.next()) ++pages;
    return pages;
}

} // namespace

class NativePrintTest : public QObject {
    Q_OBJECT
private slots:
    void destinationsListPrintersAndTheFileEngine()
    {
        NativePrintBackend backend;
        const QVector<Print::Capabilities> destinations = backend.destinations();
        QCOMPARE(destinations.size(), QPrinterInfo::availablePrinterNames().size() + 1);
        const Print::Capabilities &file = destinations.last();
        QCOMPARE(file.printerId, NativePrintBackend::fileDestinationId());
        QCOMPARE(file.kind, QStringLiteral("file"));
        QVERIFY(file.colorModes.contains(QStringLiteral("grayscale")));
        for (const Print::Capabilities &printer : destinations) {
            if (printer.kind != QLatin1String("printer")) continue;
            QVERIFY(printer.printerId.startsWith(QStringLiteral("printer:")));
            QVERIFY(printer.duplexModes.contains(QStringLiteral("simplex")));
            QVERIFY(!printer.colorModes.isEmpty());
        }
    }

    void systemEnginePrintsTheRangeToAFile()
    {
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("system.pdf"));
        PrintAdapter adapter;
        adapter.setBackends({std::make_shared<NativePrintBackend>()});
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), NativePrintBackend::fileDestinationId()},
                                       {QStringLiteral("outputPath"), path},
                                       {QStringLiteral("pages"), QStringLiteral("1-2")},
                                       {QStringLiteral("paperId"), QStringLiteral("a4")}}));
        QVERIFY2(!adapter.print().isEmpty(), qPrintable(adapter.lastError()));
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QVERIFY(QFile::exists(path));
        QVERIFY(!QFile::exists(path + QStringLiteral(".part")));
        QSizeF box;
        QCOMPARE(pdfPages(path, &box), 2);
        QVERIFY2(qAbs(box.width() - 595.28) <= 1.0 && qAbs(box.height() - 841.89) <= 1.0,
                 qPrintable(QStringLiteral("%1 x %2").arg(box.width()).arg(box.height())));
    }

    void cancellationAndVanishedPrintersRecover()
    {
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("cancelled.pdf"));
        auto backend = std::make_shared<NativePrintBackend>();
        PrintAdapter adapter;
        adapter.setBackends({backend});
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), NativePrintBackend::fileDestinationId()},
                                       {QStringLiteral("outputPath"), path}}));
        const QString id = adapter.print();
        QVERIFY(adapter.cancel(id));
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
        QVERIFY(!QFile::exists(path));
        QVERIFY(!QFile::exists(path + QStringLiteral(".part")));

        // A printer that is not (or no longer) installed is refused, not replaced.
        Print::Capabilities gone;
        gone.printerId = NativePrintBackend::printerId(QStringLiteral("qindatk-no-such-printer"));
        QString error;
        QVERIFY(!backend->open({}, gone, &error));
        QVERIFY(error.contains(QStringLiteral("qindatk-no-such-printer")));
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), gone.printerId}}));
        QVERIFY(adapter.print().isEmpty());
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("unavailable"));

        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), NativePrintBackend::fileDestinationId()}}));
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(pdfPages(path, nullptr), 3);
    }

    void jobOptionsFollowTheTicket()
    {
        Print::Ticket ticket;
        ticket.copies = 3;
        ticket.collate = true;
        ticket.duplex = QStringLiteral("long-edge");
        ticket.colorMode = QStringLiteral("grayscale");
        ticket.paperId = QStringLiteral("a4");
        using O = PrintSpooler::Options;
        QCOMPARE(NativePrintBackend::jobOptions(ticket),
                 (O{{QStringLiteral("copies"), QStringLiteral("3")}, {QStringLiteral("collate"), QStringLiteral("true")},
                    {QStringLiteral("sides"), QStringLiteral("two-sided-long-edge")},
                    {QStringLiteral("print-color-mode"), QStringLiteral("monochrome")},
                    {QStringLiteral("media"), QStringLiteral("iso_a4_210x297mm")}}));
        ticket.copies = 1;
        ticket.duplex = QStringLiteral("short-edge");
        ticket.colorMode = QStringLiteral("color");
        ticket.paperId = QStringLiteral("native:Env10");
        QCOMPARE(NativePrintBackend::jobOptions(ticket),
                 (O{{QStringLiteral("copies"), QStringLiteral("1")},
                    {QStringLiteral("sides"), QStringLiteral("two-sided-short-edge")},
                    {QStringLiteral("print-color-mode"), QStringLiteral("color")},
                    {QStringLiteral("media"), QStringLiteral("Env10")}}));
        ticket.duplex = QStringLiteral("simplex");
        ticket.paperId = QStringLiteral("document"); // the PDF's own page size
        QCOMPARE(NativePrintBackend::jobOptions(ticket).at(1).second, QStringLiteral("one-sided"));
        QCOMPARE(NativePrintBackend::jobOptions(ticket).size(), 3);
    }

    void printerJobsCountOnlyWhenTheQueueAccepts()
    {
        QTemporaryDir scratch;
        QTemporaryDir temp; // the job's temporary PDF lives here, and must not stay
        QVERIFY(scratch.isValid() && temp.isValid());
        const QByteArray oldTemp = qgetenv("TMPDIR");
        qputenv("TMPDIR", QFile::encodeName(temp.path()));
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        auto spooler = std::make_shared<FakeSpooler>();
        PrintAdapter adapter;
        adapter.setBackends({std::make_shared<NativePrintBackend>(spooler)});
        adapter.setDocument(&doc);
        const QVariantMap ticket{{QStringLiteral("printerId"), NativePrintBackend::printerId(QStringLiteral("ghost"))},
                                 {QStringLiteral("copies"), 3}, {QStringLiteral("collate"), true},
                                 {QStringLiteral("duplex"), QStringLiteral("long-edge")},
                                 {QStringLiteral("colorMode"), QStringLiteral("grayscale")},
                                 {QStringLiteral("paperId"), QStringLiteral("a4")}, {QStringLiteral("pages"), QStringLiteral("1-2")}};
        QVERIFY2(adapter.applySettings(ticket), qPrintable(adapter.lastError()));
        const QVariantMap before = adapter.settings();

        // A queue that refuses the rendered job: failed, never "completed".
        spooler->accept = false;
        QVERIFY2(!adapter.print().isEmpty(), qPrintable(adapter.lastError()));
        QTRY_VERIFY(!adapter.busy());
        QVariantMap job = adapter.job();
        QCOMPARE(job.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QVERIFY(job.value(QStringLiteral("error")).toString().contains(QStringLiteral("not-accepting-jobs")));
        QCOMPARE(job.value(QStringLiteral("affectedPages")).toString(), QStringLiteral("1-2"));
        QCOMPARE(adapter.lastErrorCode(), QStringLiteral("backendFailure"));
        QCOMPARE(adapter.settings(), before);
        QCOMPARE(spooler->received.size(), 1);
        QCOMPARE(entries(temp.path()), QStringList());

        // Accepted: the job carries the ticket's options and the selected sheets.
        spooler->accept = true;
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(spooler->received.size(), 2);
        const FakeSpooler::Received &sent = spooler->received.last();
        QCOMPARE(sent.job.printerName, QStringLiteral("ghost"));
        QCOMPARE(sent.job.options, NativePrintBackend::jobOptions(adapter.ticket()));
        QVERIFY(sent.job.options.contains({QStringLiteral("copies"), QStringLiteral("3")}));
        QVERIFY(sent.job.options.contains({QStringLiteral("sides"), QStringLiteral("two-sided-long-edge")}));
        QVERIFY(sent.job.options.contains({QStringLiteral("print-color-mode"), QStringLiteral("monochrome")}));
        const QString copy = scratch.filePath(QStringLiteral("sent.pdf"));
        QVERIFY(writeText(copy, sent.pdf));
        QSizeF box;
        QCOMPARE(pdfPages(copy, &box), 2); // one of each sheet; copies are the queue's job
        QVERIFY(qAbs(box.width() - 595.28) <= 1.0 && qAbs(box.height() - 841.89) <= 1.0);
        QCOMPARE(entries(temp.path()), QStringList());

        // A queue that stopped accepting is refused before anything renders.
        spooler->refusalReason = QStringLiteral("ghost is not accepting jobs");
        QVERIFY(!adapter.print().isEmpty());
        QTRY_VERIFY(!adapter.busy());
        QCOMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(adapter.job().value(QStringLiteral("error")).toString(), spooler->refusalReason);
        QCOMPARE(spooler->received.size(), 2);
        if (oldTemp.isNull()) qunsetenv("TMPDIR");
        else qputenv("TMPDIR", oldTemp);
    }

    void cupsRefusalIsReportedNotIgnored()
    {
        if (!CupsSpooler::available()) QSKIP("built without libcups");
        // No queue by this name exists, with or without a running scheduler:
        // the real submission path must report failure, not a job.
        const QString name = QStringLiteral("qindatk-no-such-queue-%1").arg(QCoreApplication::applicationPid());
        CupsSpooler cups;
        QVERIFY(!cups.refusal(name).isEmpty());
        QTemporaryDir directory;
        const QString pdf = directory.filePath(QStringLiteral("job.pdf"));
        QVERIFY(writeText(pdf, "%PDF-1.4\n%%EOF\n"));
        QString error;
        QCOMPARE(cups.submit({name, QStringLiteral("QindaTK test"), pdf, {}}, &error), 0);
        QVERIFY(!error.isEmpty());
    }

    void printToFileReplacesAtomicallyOrNotAtAll()
    {
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString folder = directory.filePath(QStringLiteral("out"));
        QVERIFY(QDir().mkpath(folder));
        const QString path = folder + QStringLiteral("/report.pdf");
        const QString part = path + QStringLiteral(".part"); // somebody else's file
        QVERIFY(writeText(path, "previous report"));
        QVERIFY(writeText(part, "unrelated"));
        PrintAdapter adapter;
        adapter.setBackends({std::make_shared<NativePrintBackend>()});
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("printerId"), NativePrintBackend::fileDestinationId()},
                                       {QStringLiteral("outputPath"), path}}));

        // A folder that cannot take the new file: the old one stays whole.
        QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner));
        if (QFile file(folder + QStringLiteral("/probe")); file.open(QIODevice::WriteOnly)) {
            QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            QSKIP("running with permission to write into read-only folders");
        }
        QVERIFY(!adapter.print().isEmpty());
        QTRY_VERIFY(!adapter.busy());
        QCOMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(adapter.job().value(QStringLiteral("failedPage")).toInt(), 1);
        QVERIFY(adapter.job().value(QStringLiteral("error")).toString().contains(path));
        QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(readAll(path), QStringLiteral("previous report"));
        QCOMPARE(entries(folder), (QStringList{QStringLiteral("report.pdf"), QStringLiteral("report.pdf.part")}));

        // No usable temporary folder: fails on the first sheet, target untouched.
        const QByteArray oldTemp = qgetenv("TMPDIR");
        qputenv("TMPDIR", QFile::encodeName(directory.filePath(QStringLiteral("no-such-temp"))));
        // AGENT-NOTE: Qt 6.8 answers a missing TMPDIR with a usable fallback
        // folder (QDir::tempPath()), so there is no failure to observe there;
        // Qt 6.11 keeps the missing path and the job must fail cleanly.
        const bool qtFellBack = QFileInfo(QDir::tempPath()).isDir();
        if (!qtFellBack) {
            QVERIFY(!adapter.print().isEmpty());
            QTRY_VERIFY(!adapter.busy());
        }
        if (oldTemp.isNull()) qunsetenv("TMPDIR");
        else qputenv("TMPDIR", oldTemp);
        if (!qtFellBack) {
            QCOMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
            QVERIFY(adapter.job().value(QStringLiteral("error")).toString().contains(QStringLiteral("temporary file")));
        }
        QCOMPARE(readAll(path), QStringLiteral("previous report"));

        // Success replaces the target in one step and leaves the sibling alone.
        QVERIFY(!adapter.print().isEmpty());
        QTRY_COMPARE(adapter.job().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(pdfPages(path, nullptr), 3);
        QCOMPARE(readAll(part), QStringLiteral("unrelated"));
        QCOMPARE(entries(folder), (QStringList{QStringLiteral("report.pdf"), QStringLiteral("report.pdf.part")}));
    }

    void registeredBackendsReachNewAdapters()
    {
        PrintAdapter::registerDefaultBackend(std::make_shared<NativePrintBackend>());
        PrintAdapter adapter;
        QStringList ids;
        for (const QVariant &destination : adapter.destinations())
            ids.append(destination.toMap().value(QStringLiteral("printerId")).toString());
        PrintAdapter::clearDefaultBackends();
        QCOMPARE(ids.first(), QStringLiteral("pdf"));
        QVERIFY(ids.contains(NativePrintBackend::fileDestinationId()));
    }
};

QTEST_MAIN(NativePrintTest)
#include "tst_native_print.moc"
