// SPDX-License-Identifier: LGPL-3.0-or-later
#include "native_print_backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QSaveFile>
#include <QSet>
#include <QFileInfo>
#include <QTemporaryFile>

#ifndef QINDATK_HAVE_CUPS
#define QINDATK_HAVE_CUPS 0
#endif
#if QINDATK_HAVE_CUPS
#include <cups/cups.h>
#endif

namespace QindaTK {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("QindaTK::Print", text); }

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QString paperIdFor(const QPageSize &size)
{
    switch (size.id()) {
    case QPageSize::A3: return QStringLiteral("a3");
    case QPageSize::A4: return QStringLiteral("a4");
    case QPageSize::A5: return QStringLiteral("a5");
    case QPageSize::Letter: return QStringLiteral("letter");
    case QPageSize::Legal: return QStringLiteral("legal");
    case QPageSize::Tabloid: return QStringLiteral("tabloid");
    default: return QStringLiteral("native:") + size.key();
    }
}

Print::Capabilities capabilitiesOf(const QPrinterInfo &info)
{
    Print::Capabilities c;
    c.printerId = NativePrintBackend::printerId(info.printerName());
    c.label = info.description().isEmpty() ? info.printerName() : info.description();
    c.kind = QStringLiteral("printer");
    const QPrinter::PrinterState state = info.state();
    c.available = state == QPrinter::Idle || state == QPrinter::Active;
    c.status = state == QPrinter::Idle ? tr("Ready") : state == QPrinter::Active ? tr("Printing")
             : state == QPrinter::Aborted ? tr("Stopped") : tr("Error");
    if (info.supportsCustomPageSizes()) c.papers.append(Print::standardPapers().first()); // document size
    else c.unavailable.append(tr("Document page size"));
    QSet<QString> seen;
    for (const QPageSize &size : info.supportedPageSizes()) {
        const QString id = paperIdFor(size);
        if (seen.contains(id)) continue;
        seen.insert(id);
        QSizeF points = size.size(QPageSize::Point);
        if (points.width() > points.height()) points.transpose();
        c.papers.append({id, size.name(), points});
    }
    c.duplexModes.clear();
    for (const QPrinter::DuplexMode mode : info.supportedDuplexModes()) {
        if (mode == QPrinter::DuplexNone) c.duplexModes.append(QStringLiteral("simplex"));
        else if (mode == QPrinter::DuplexLongSide) c.duplexModes.append(QStringLiteral("long-edge"));
        else if (mode == QPrinter::DuplexShortSide) c.duplexModes.append(QStringLiteral("short-edge"));
    }
    if (!c.duplexModes.contains(QStringLiteral("simplex"))) c.duplexModes.prepend(QStringLiteral("simplex"));
    if (!c.duplexModes.contains(QStringLiteral("long-edge"))) c.unavailable.append(tr("Two-sided printing"));
    c.colorModes.clear();
    for (const QPrinter::ColorMode mode : info.supportedColorModes())
        c.colorModes.append(mode == QPrinter::Color ? QStringLiteral("color") : QStringLiteral("grayscale"));
    if (c.colorModes.isEmpty()) c.colorModes.append(QStringLiteral("grayscale"));
    c.colorModes.removeDuplicates();
    if (!c.colorModes.contains(QStringLiteral("color"))) c.unavailable.append(tr("Color output"));
    if (!c.colorModes.contains(QStringLiteral("grayscale"))) c.unavailable.append(tr("Grayscale output"));
    QPrinter printer(info, QPrinter::HighResolution);
    c.maxCopies = printer.supportsMultipleCopies() ? 999 : 1;
    c.collate = printer.supportsMultipleCopies();
    if (c.maxCopies == 1) c.unavailable.append(tr("Multiple copies"));
    QPageLayout layout = printer.pageLayout();
    layout.setUnits(QPageLayout::Point);
    c.printableMarginsPt = layout.minimumMargins();
    return c;
}

// One job: Qt's PDF engine renders into a private temporary file; finish()
// either commits it to the target through QSaveFile or submits it to the
// spooler. Nothing outside the temporary file and QSaveFile's own sibling
// exists before finish() succeeds, and an existing target is replaced
// atomically or not at all.
class NativePrintDevice final : public Print::Device {
public:
    NativePrintDevice(const Print::Ticket &ticket, const QString &filePath, const QString &printerName,
                      std::shared_ptr<PrintSpooler> spooler)
        : m_printer(QPrinter::HighResolution), m_filePath(filePath), m_printerName(printerName),
          m_spooler(std::move(spooler)), m_options(NativePrintBackend::jobOptions(ticket))
    {
        m_name = filePath.isEmpty() ? printerName : filePath;
        m_printer.setOutputFormat(QPrinter::PdfFormat);
        m_printer.setResolution(int(Print::DeviceDpi));
        m_printer.setFullPage(true);
        m_printer.setDocName(tr("QindaTK document"));
        // Copies, collation and sides are job options for the spooler; the
        // PDF holds each sheet once.
        m_printer.setColorMode(ticket.colorMode == QLatin1String("grayscale") ? QPrinter::GrayScale : QPrinter::Color);
    }
    ~NativePrintDevice() override { abort(); }

    bool beginSheet(const Print::Sheet &sheet, int index, QString *error) override
    {
        if (m_done) return fail(error, tr("The print job is already closed"));
        if (index == 0 && !start(error)) return false;
        const QPageLayout layout = Print::pageLayoutFor(sheet.sizePt);
        const bool sameSize = m_printer.pageLayout().fullRect(QPageLayout::Point).size() == layout.fullRect(QPageLayout::Point).size();
        if (!sameSize && !m_printer.setPageLayout(layout))
            return fail(error, tr("%1 does not accept a %2 × %3 pt sheet").arg(m_name)
                                   .arg(qRound(sheet.sizePt.width())).arg(qRound(sheet.sizePt.height())));
        if (index == 0) {
            if (!m_painter.begin(&m_printer)) return fail(error, tr("Cannot start printing on %1").arg(m_name));
            return true;
        }
        if (!m_painter.isActive()) return fail(error, tr("The print job was not started"));
        if (!m_printer.newPage()) return fail(error, tr("%1 refused page %2").arg(m_name).arg(index + 1));
        return true;
    }
    QPainter *painter() override { return &m_painter; }
    bool finish(QString *error) override
    {
        if (m_done) return fail(error, tr("The print job is already closed"));
        m_done = true;
        const bool ended = m_painter.isActive() && m_painter.end();
        const QPrinter::PrinterState state = m_printer.printerState();
        if (!ended || state == QPrinter::Error || state == QPrinter::Aborted) {
            discard();
            return fail(error, tr("%1 reported an error; nothing was printed").arg(m_name));
        }
        const bool ok = m_filePath.isEmpty() ? submit(error) : commit(error);
        discard();
        return ok;
    }
    void abort() override
    {
        if (m_done) return;
        m_done = true;
        if (m_painter.isActive()) {
            m_printer.abort();
            m_painter.end();
        }
        discard();
    }

private:
    bool start(QString *error)
    {
        // AGENT-GUARD: check the folder ourselves. Qt versions differ in how
        // QTemporaryFile treats a missing temporary folder (Qt 6.8 still
        // creates the file), and a job must fail the same way on all of them.
        const QFileInfo tempDir(QDir::tempPath());
        if (!tempDir.isDir() || !tempDir.isWritable())
            return fail(error, tr("Cannot create a temporary file for printing: %1 is not a writable folder")
                                   .arg(QDir::toNativeSeparators(tempDir.filePath())));
        m_temp.setFileTemplate(QDir::temp().filePath(QStringLiteral("qindatk-print-XXXXXX.pdf")));
        if (!m_temp.open())
            return fail(error, tr("Cannot create a temporary file for printing: %1").arg(m_temp.errorString()));
        m_temp.close(); // the print engine opens it by name; QTemporaryFile still removes it
        m_printer.setOutputFileName(m_temp.fileName());
        if (m_filePath.isEmpty()) return true;
        // Opened now so an unwritable folder fails on the first sheet, not
        // after the whole job has been rendered.
        m_target = std::make_unique<QSaveFile>(m_filePath);
        if (!m_target->open(QIODevice::WriteOnly))
            return fail(error, tr("Cannot write %1: %2").arg(m_filePath, m_target->errorString()));
        return true;
    }
    bool commit(QString *error)
    {
        QFile rendered(m_temp.fileName());
        if (!m_target || !rendered.open(QIODevice::ReadOnly))
            return fail(error, tr("Cannot read the rendered job for %1").arg(m_filePath));
        while (!rendered.atEnd()) {
            const QByteArray chunk = rendered.read(1 << 20);
            if (chunk.isEmpty() || m_target->write(chunk) != chunk.size())
                return fail(error, tr("Cannot write %1: %2").arg(m_filePath, m_target->errorString()));
        }
        if (!m_target->commit()) return fail(error, tr("Cannot write %1: %2").arg(m_filePath, m_target->errorString()));
        m_target.reset();
        return true;
    }
    bool submit(QString *error)
    {
        if (!m_spooler) return fail(error, tr("No print spooler is available"));
        const PrintSpooler::Job job{m_printerName, m_printer.docName(), m_temp.fileName(), m_options};
        QString reason;
        if (m_spooler->submit(job, &reason) > 0) return true;
        return fail(error, tr("%1 did not accept the job (%2); nothing was printed")
                               .arg(m_name, reason.isEmpty() ? tr("no reason given") : reason));
    }
    void discard()
    {
        if (m_target) m_target->cancelWriting();
        m_target.reset(); // QSaveFile removes its own temporary file; the target is untouched
        if (!m_temp.fileName().isEmpty()) m_temp.remove(); // empty until start() opened it
    }

    QPrinter m_printer;
    QPainter m_painter;
    QTemporaryFile m_temp;
    std::unique_ptr<QSaveFile> m_target;
    QString m_filePath;
    QString m_printerName;
    QString m_name;
    std::shared_ptr<PrintSpooler> m_spooler;
    PrintSpooler::Options m_options;
    bool m_done = false;
};

QString mediaFor(const QString &paperId)
{
    static const QHash<QString, QString> pwg{
        {QStringLiteral("a3"), QStringLiteral("iso_a3_297x420mm")},
        {QStringLiteral("a4"), QStringLiteral("iso_a4_210x297mm")},
        {QStringLiteral("a5"), QStringLiteral("iso_a5_148x210mm")},
        {QStringLiteral("letter"), QStringLiteral("na_letter_8.5x11in")},
        {QStringLiteral("legal"), QStringLiteral("na_legal_8.5x14in")},
        {QStringLiteral("tabloid"), QStringLiteral("na_ledger_11x17in")}};
    if (paperId.startsWith(QLatin1String("native:"))) return paperId.mid(7);
    return pwg.value(paperId);
}

} // namespace

bool CupsSpooler::available()
{
    return QINDATK_HAVE_CUPS;
}

QVector<Print::Capabilities> CupsSpooler::printers()
{
    QVector<Print::Capabilities> list;
    if (!available()) return list;
    const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    for (const QPrinterInfo &info : printers) list.append(capabilitiesOf(info));
    return list;
}

QString CupsSpooler::refusal(const QString &printerName)
{
#if QINDATK_HAVE_CUPS
    cups_dest_t *dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, printerName.toUtf8().constData(), nullptr);
    if (!dest) return tr("%1 is no longer available").arg(printerName);
    const char *accepting = cupsGetOption("printer-is-accepting-jobs", dest->num_options, dest->options);
    const char *state = cupsGetOption("printer-state", dest->num_options, dest->options);
    QString reason;
    if (accepting && qstrcmp(accepting, "false") == 0) reason = tr("%1 is not accepting jobs").arg(printerName);
    else if (state && qstrcmp(state, "5") == 0) reason = tr("%1 is stopped").arg(printerName); // IPP printer-state stopped
    cupsFreeDests(1, dest);
    return reason;
#else
    return tr("%1 cannot be reached: this build has no CUPS support").arg(printerName);
#endif
}

int CupsSpooler::submit(const Job &job, QString *error)
{
#if QINDATK_HAVE_CUPS
    int count = 0;
    cups_option_t *options = nullptr;
    for (const auto &option : job.options)
        count = cupsAddOption(option.first.toUtf8().constData(), option.second.toUtf8().constData(), count, &options);
    // AGENT-NOTE (PLT-04): Qt's own CUPS engine ignores this call's result,
    // so a refused job looked printed; the job ID is the proof of acceptance.
    const int id = cupsPrintFile2(CUPS_HTTP_DEFAULT, job.printerName.toUtf8().constData(),
                                  QFile::encodeName(job.pdfPath).constData(), job.title.toUtf8().constData(),
                                  count, options);
    cupsFreeOptions(count, options);
    if (id <= 0 && error) *error = QString::fromUtf8(cupsLastErrorString());
    return qMax(id, 0);
#else
    Q_UNUSED(job);
    if (error) *error = tr("this build has no CUPS support");
    return 0;
#endif
}

NativePrintBackend::NativePrintBackend(std::shared_ptr<PrintSpooler> spooler)
    : m_spooler(spooler ? std::move(spooler) : std::make_shared<CupsSpooler>())
{
}

PrintSpooler::Options NativePrintBackend::jobOptions(const Print::Ticket &ticket)
{
    const QString copies = QString::number(qMax(1, ticket.copies));
    PrintSpooler::Options options{{QStringLiteral("copies"), copies}};
    if (ticket.copies > 1) options.append({QStringLiteral("collate"), ticket.collate ? QStringLiteral("true") : QStringLiteral("false")});
    options.append({QStringLiteral("sides"), ticket.duplex == QLatin1String("long-edge") ? QStringLiteral("two-sided-long-edge")
                                              : ticket.duplex == QLatin1String("short-edge") ? QStringLiteral("two-sided-short-edge")
                                                                                             : QStringLiteral("one-sided")});
    options.append({QStringLiteral("print-color-mode"),
                    ticket.colorMode == QLatin1String("grayscale") ? QStringLiteral("monochrome") : QStringLiteral("color")});
    if (const QString media = mediaFor(ticket.paperId); !media.isEmpty()) options.append({QStringLiteral("media"), media});
    return options;
}

QVector<Print::Capabilities> NativePrintBackend::destinations()
{
    QVector<Print::Capabilities> list = m_spooler->printers();
    Print::Capabilities file = Print::PdfFileBackend::capabilities();
    file.printerId = fileDestinationId();
    file.label = tr("Print to file (system print engine)");
    list.append(file);
    return list;
}

std::unique_ptr<Print::Device> NativePrintBackend::open(const Print::Ticket &ticket, const Print::Capabilities &destination,
                                                        QString *error)
{
    if (destination.printerId == fileDestinationId()) {
        const QString path = Print::localPath(ticket.outputPath);
        if (path.isEmpty()) {
            fail(error, tr("Printing to a file requires a local file path"));
            return nullptr;
        }
        return std::make_unique<NativePrintDevice>(ticket, path, QString(), nullptr);
    }
    const QString prefix = printerId(QString());
    if (!destination.printerId.startsWith(prefix)) {
        fail(error, tr("%1 is not a system printer").arg(destination.printerId));
        return nullptr;
    }
    const QString name = destination.printerId.mid(prefix.size());
    if (const QString reason = m_spooler->refusal(name); !reason.isEmpty()) {
        fail(error, reason);
        return nullptr;
    }
    return std::make_unique<NativePrintDevice>(ticket, QString(), name, m_spooler);
}

} // namespace QindaTK
