// SPDX-License-Identifier: LGPL-3.0-or-later
#include "native_print_backend.h"

#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QSet>

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

class NativePrintDevice final : public Print::Device {
public:
    NativePrintDevice(const Print::Ticket &ticket, const QString &printerName, const QString &filePath)
        : m_printer(QPrinter::HighResolution), m_filePath(filePath)
    {
        if (filePath.isEmpty()) {
            m_printer.setPrinterName(printerName);
            m_name = printerName;
        } else {
            // The engine writes a temporary file that becomes the target on finish.
            m_printer.setOutputFormat(QPrinter::PdfFormat);
            m_partPath = filePath + QStringLiteral(".part");
            m_printer.setOutputFileName(m_partPath);
            m_name = filePath;
        }
        m_printer.setResolution(int(Print::DeviceDpi));
        m_printer.setFullPage(true);
        m_printer.setDocName(tr("QindaTK document"));
        m_printer.setCopyCount(ticket.copies);
        m_printer.setCollateCopies(ticket.collate);
        m_printer.setDuplex(ticket.duplex == QLatin1String("long-edge") ? QPrinter::DuplexLongSide
                            : ticket.duplex == QLatin1String("short-edge") ? QPrinter::DuplexShortSide
                                                                           : QPrinter::DuplexNone);
        m_printer.setColorMode(ticket.colorMode == QLatin1String("grayscale") ? QPrinter::GrayScale : QPrinter::Color);
    }
    ~NativePrintDevice() override { abort(); }

    bool beginSheet(const Print::Sheet &sheet, int index, QString *error) override
    {
        if (m_done) return fail(error, tr("The print job is already closed"));
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
            removePart();
            return fail(error, tr("%1 reported an error; nothing was printed").arg(m_name));
        }
        if (!m_partPath.isEmpty()) {
            if (QFile::exists(m_filePath) && !QFile::remove(m_filePath)) {
                removePart();
                return fail(error, tr("Cannot replace %1").arg(m_filePath));
            }
            if (!QFile::rename(m_partPath, m_filePath)) {
                removePart();
                return fail(error, tr("Cannot write %1").arg(m_filePath));
            }
        }
        return true;
    }
    void abort() override
    {
        if (m_done) return;
        m_done = true;
        if (m_painter.isActive()) {
            m_printer.abort(); // the spool job is dropped before CUPS receives it
            m_painter.end();
        }
        removePart();
    }

private:
    void removePart()
    {
        if (!m_partPath.isEmpty()) QFile::remove(m_partPath);
    }
    QPrinter m_printer;
    QPainter m_painter;
    QString m_filePath;
    QString m_partPath;
    QString m_name;
    bool m_done = false;
};

} // namespace

QVector<Print::Capabilities> NativePrintBackend::destinations()
{
    QVector<Print::Capabilities> list;
    const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    for (const QPrinterInfo &info : printers) list.append(capabilitiesOf(info));
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
        return std::make_unique<NativePrintDevice>(ticket, QString(), path);
    }
    const QString prefix = printerId(QString());
    if (!destination.printerId.startsWith(prefix)) {
        fail(error, tr("%1 is not a system printer").arg(destination.printerId));
        return nullptr;
    }
    const QString name = destination.printerId.mid(prefix.size());
    if (QPrinterInfo::printerInfo(name).isNull()) {
        fail(error, tr("%1 is no longer available").arg(name));
        return nullptr;
    }
    return std::make_unique<NativePrintDevice>(ticket, name, QString());
}

} // namespace QindaTK
