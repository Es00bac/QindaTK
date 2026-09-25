// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_output.h"

#include <QList>
#include <QPair>

namespace QindaTK {

// Where finished printer jobs go. The default is CUPS (CupsSpooler); tests
// pass their own to watch submissions and refusals without a print server.
class PrintSpooler {
public:
    using Options = QList<QPair<QString, QString>>;
    struct Job {
        QString printerName;
        QString title;
        QString pdfPath;   // the rendered job; it exists only during submit()
        Options options;   // NativePrintBackend::jobOptions(ticket)
    };
    virtual ~PrintSpooler() = default;
    // The printers this spooler can reach, with their capabilities.
    virtual QVector<Print::Capabilities> printers() = 0;
    // Empty when the queue exists and accepts jobs; otherwise the reason.
    virtual QString refusal(const QString &printerName) = 0;
    // Hands `job` to the queue: the spooler's job ID (> 0), or 0 and `error`.
    virtual int submit(const Job &job, QString *error) = 0;
};

// CUPS through libcups: printers from QPrinterInfo, queue state from
// cupsGetNamedDest, and cupsPrintFile2 whose job ID proves acceptance.
// Without libcups at build time it lists no printers and refuses every job.
class CupsSpooler final : public PrintSpooler {
public:
    static bool available();
    QVector<Print::Capabilities> printers() override;
    QString refusal(const QString &printerName) override;
    int submit(const Job &job, QString *error) override;
};

// The platform's printers through a spooler (CUPS on Linux), plus
// "print-to-file": Qt's PDF print engine, committed through QSaveFile.
// Optional (qindatk_widgets, built when Qt PrintSupport is found); register it
// once with PrintAdapter::registerDefaultBackend after creating the
// application object. Destination IDs: "printer:<name>" and "print-to-file".
// AGENT-NOTE (D-131): every device renders into a private temporary PDF.
// finish() commits it (atomic replace of the target file) or submits it to
// the spooler, and only a spooler job ID counts as success; before that,
// abort() discards everything, so deliveredSheets() stays 0.
class NativePrintBackend final : public Print::Backend {
public:
    explicit NativePrintBackend(std::shared_ptr<PrintSpooler> spooler = {});
    static QString printerId(const QString &printerName) { return QStringLiteral("printer:") + printerName; }
    static QString fileDestinationId() { return QStringLiteral("print-to-file"); }
    // AGENT-CONTRACT (PLT-04, docs/printing.md#native-printers): the job
    // options a printer receives for `ticket`, as CUPS/IPP names: copies,
    // collate, sides, print-color-mode and media. tst_native_print checks
    // this table against the submitted job.
    static PrintSpooler::Options jobOptions(const Print::Ticket &ticket);
    QVector<Print::Capabilities> destinations() override;
    std::unique_ptr<Print::Device> open(const Print::Ticket &ticket, const Print::Capabilities &destination,
                                        QString *error) override;

private:
    std::shared_ptr<PrintSpooler> m_spooler;
};

} // namespace QindaTK
