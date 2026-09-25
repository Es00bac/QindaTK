// SPDX-License-Identifier: LGPL-3.0-or-later
// The native print backend (WP-053): system printers as destinations, the
// system print engine writing a file, cancellation without output, and a
// vanished printer refused while the adapter keeps working. Real printer
// runs are qualification work (WP-089); CUPS may list no printers here.
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
