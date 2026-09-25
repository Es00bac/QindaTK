// SPDX-License-Identifier: LGPL-3.0-or-later
#include "print_output.h"

#include "rich_text_layout.h"
#include "rich_text_objects.h"
#include <QCoreApplication>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QUrl>
#include <QtMath>

namespace QindaTK::Print {
namespace {

using RichText::PageSetup;
using RichText::PaintContext;

constexpr double UnitsPerPoint = DeviceDpi / 72.0;

QString tr(const char *text) { return QCoreApplication::translate("QindaTK::Print", text); }

bool setError(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

void paintCropMarks(QPainter *painter, const Sheet &sheet)
{
    // Marks sit outside the bleed so trimming never leaves them on the page.
    painter->save();
    painter->setClipping(false);
    painter->setPen(QPen(Qt::black, 0.25));
    const QRectF trim = sheet.trimPt;
    const double gap = sheet.bleedPt;
    const double length = sheet.markPt;
    for (const double y : {trim.top(), trim.bottom()}) {
        painter->drawLine(QPointF(trim.left() - gap - length, y), QPointF(trim.left() - gap, y));
        painter->drawLine(QPointF(trim.right() + gap, y), QPointF(trim.right() + gap + length, y));
    }
    for (const double x : {trim.left(), trim.right()}) {
        painter->drawLine(QPointF(x, trim.top() - gap - length), QPointF(x, trim.top() - gap));
        painter->drawLine(QPointF(x, trim.bottom() + gap), QPointF(x, trim.bottom() + gap + length));
    }
    painter->restore();
}

class PdfFileDevice final : public Device {
public:
    PdfFileDevice(const QString &path, bool grayscale) : m_file(path), m_grayscale(grayscale) {}
    ~PdfFileDevice() override { abort(); }

    bool beginSheet(const Sheet &sheet, int index, QString *error) override
    {
        if (m_done) return setError(error, tr("The PDF is already closed"));
        const QPageLayout layout = pageLayoutFor(sheet.sizePt);
        if (index == 0) {
            if (!m_file.open(QIODevice::WriteOnly))
                return setError(error, tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));
            m_writer = std::make_unique<QPdfWriter>(&m_file);
            m_writer->setResolution(int(DeviceDpi));
            m_writer->setCreator(QStringLiteral("QindaTK"));
            m_writer->setColorModel(m_grayscale ? QPdfWriter::ColorModel::Grayscale : QPdfWriter::ColorModel::RGB);
            m_writer->setPageLayout(layout);
            if (!m_painter.begin(m_writer.get())) return setError(error, tr("Cannot start the PDF"));
            return true;
        }
        if (!m_writer || !m_painter.isActive()) return setError(error, tr("The PDF was not started"));
        m_writer->setPageLayout(layout); // takes effect on the new page
        if (!m_writer->newPage()) return setError(error, tr("Cannot add PDF page %1").arg(index + 1));
        return true;
    }
    QPainter *painter() override { return &m_painter; }
    bool finish(QString *error) override
    {
        if (m_done) return setError(error, tr("The PDF is already closed"));
        m_done = true;
        const bool painted = m_painter.isActive() && m_painter.end();
        m_writer.reset();
        if (!painted) {
            m_file.cancelWriting();
            return setError(error, tr("PDF output failed for %1").arg(m_file.fileName()));
        }
        if (!m_file.commit())
            return setError(error, tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));
        return true;
    }
    void abort() override
    {
        if (m_done) return;
        m_done = true;
        if (m_painter.isActive()) m_painter.end();
        m_writer.reset();
        m_file.cancelWriting(); // QSaveFile removes its temporary file; the target stays untouched
    }

private:
    QSaveFile m_file;
    std::unique_ptr<QPdfWriter> m_writer;
    QPainter m_painter;
    bool m_grayscale = false;
    bool m_done = false;
};

} // namespace

QPageLayout pageLayoutFor(const QSizeF &sizePt)
{
    const bool landscape = sizePt.width() > sizePt.height();
    const QSizeF portrait = landscape ? sizePt.transposed() : sizePt;
    return QPageLayout(QPageSize(portrait, QPageSize::Point, QString(), QPageSize::ExactMatch),
                       landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(), QPageLayout::Point);
}

Sheet sheetFor(const PageSetup &page, int sourcePage, const Ticket &ticket, const Paper &paper)
{
    Sheet sheet;
    sheet.sourcePage = sourcePage;
    const double bleed = ticket.includeBleed ? page.bleedPt : 0.0;
    const double marks = ticket.cropMarks ? CropMarkPt : 0.0;
    const double edge = bleed + marks;
    const QSizeF trim(page.widthPt, page.heightPt);
    const QSizeF extent(trim.width() + 2 * edge, trim.height() + 2 * edge);
    if (paper.sizePt.isEmpty()) {
        // The document paper follows the page: no orientation, no scaling.
        sheet.sizePt = extent;
        sheet.trimPt = QRectF(QPointF(edge, edge), trim);
        sheet.bleedPt = bleed;
        sheet.markPt = marks;
        return sheet;
    }
    const bool landscape = ticket.orientation == QLatin1String("landscape")
        || (ticket.orientation == QLatin1String("auto") && trim.width() > trim.height());
    QSizeF size = paper.sizePt;
    if (landscape != (size.width() > size.height())) size.transpose();
    double scale = 1.0;
    if (ticket.scaleMode == QLatin1String("percent")) scale = ticket.scalePercent / 100.0;
    else if (ticket.scaleMode == QLatin1String("fit"))
        scale = qMin(size.width() / extent.width(), size.height() / extent.height());
    const QSizeF scaled = extent * scale;
    const QPointF origin((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2);
    sheet.sizePt = size;
    sheet.scale = scale;
    sheet.trimPt = QRectF(origin + QPointF(edge * scale, edge * scale), trim * scale);
    sheet.bleedPt = bleed * scale;
    sheet.markPt = marks * scale;
    sheet.clipped = scaled.width() > size.width() + 0.01 || scaled.height() > size.height() + 0.01;
    return sheet;
}

QVector<Sheet> planSheets(const PageSetup &page, int pageCount, const Ticket &ticket, const Paper &paper)
{
    QVector<Sheet> sheets;
    for (int source : selectedPages(ticket.ranges, pageCount)) sheets.append(sheetFor(page, source, ticket, paper));
    return sheets;
}

QVariantMap sheetToMap(const Sheet &sheet)
{
    return {{QStringLiteral("sourcePage"), sheet.sourcePage},
            {QStringLiteral("pageNumber"), sheet.sourcePage + 1},
            {QStringLiteral("widthPt"), sheet.sizePt.width()},
            {QStringLiteral("heightPt"), sheet.sizePt.height()},
            {QStringLiteral("trim"), QVariantMap{{QStringLiteral("x"), sheet.trimPt.x()},
                                                 {QStringLiteral("y"), sheet.trimPt.y()},
                                                 {QStringLiteral("width"), sheet.trimPt.width()},
                                                 {QStringLiteral("height"), sheet.trimPt.height()}}},
            {QStringLiteral("scale"), sheet.scale},
            {QStringLiteral("bleedPt"), sheet.bleedPt},
            {QStringLiteral("markPt"), sheet.markPt},
            {QStringLiteral("clipped"), sheet.clipped}};
}

QSize sheetPixelSize(const Sheet &sheet, double zoom)
{
    const auto pixels = [zoom](double points) {
        return qBound(0, qCeil(points * UnitsPerPoint * zoom - 1e-6), 16384);
    };
    return {pixels(sheet.sizePt.width()), pixels(sheet.sizePt.height())};
}

PaintContext outputContext(RichTextObjectRenderer *objects)
{
    PaintContext context;
    context.ink = Qt::black;
    context.link = QColor(QStringLiteral("#0645ad"));
    context.border = QColor(QStringLiteral("#888888"));
    context.objectFill = QColor(QStringLiteral("#f0f0f0"));
    context.objectText = Qt::black;
    context.marker = Qt::black;
    context.objects = objects;
    return context;
}

void paintSheet(QPainter *painter, RichTextLayout &layout, const Sheet &sheet,
                RichTextObjectRenderer *objects, bool paper)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->scale(UnitsPerPoint, UnitsPerPoint); // sheet points
    const QRectF sheetRect(QPointF(0, 0), sheet.sizePt);
    if (paper) painter->fillRect(sheetRect, Qt::white);
    painter->setClipRect(sheetRect);
    if (sheet.sourcePage >= 0 && sheet.sourcePage < layout.pageCount()) {
        painter->save();
        painter->translate(sheet.trimPt.topLeft());
        const double layoutToSheet = sheet.scale / UnitsPerPoint; // layout px to sheet points
        painter->scale(layoutToSheet, layoutToSheet);
        const double bleedPx = sheet.bleedPt / layoutToSheet;
        const QRectF area = QRectF(QPointF(0, 0), layout.pageSize()).adjusted(-bleedPx, -bleedPx, bleedPx, bleedPx);
        painter->setClipRect(area, Qt::IntersectClip);
        layout.paintPage(painter, sheet.sourcePage, outputContext(objects), area);
        painter->restore();
    }
    if (sheet.markPt > 0) paintCropMarks(painter, sheet);
    painter->restore();
}

QImage renderSheet(RichTextLayout &layout, const Sheet &sheet, double zoom, bool grayscale,
                   RichTextObjectRenderer *objects)
{
    const QSize size = sheetPixelSize(sheet, zoom);
    if (size.isEmpty() || !(zoom > 0)) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        if (zoom != 1.0) painter.scale(zoom, zoom);
        paintSheet(&painter, layout, sheet, objects, true);
    }
    if (grayscale)
        image = image.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return image;
}

Capabilities PdfFileBackend::capabilities()
{
    Capabilities pdf;
    pdf.printerId = QStringLiteral("pdf");
    pdf.label = tr("Save as PDF");
    pdf.kind = QStringLiteral("file");
    pdf.papers = standardPapers();
    pdf.colorModes = {QStringLiteral("color"), QStringLiteral("grayscale")};
    pdf.unavailable = {tr("Two-sided output (PDF file)"), tr("Multiple copies (PDF file)")};
    return pdf;
}

std::unique_ptr<Device> PdfFileBackend::open(const Ticket &ticket, const Capabilities &, QString *error)
{
    const QString path = localPath(ticket.outputPath);
    if (path.isEmpty()) {
        setError(error, tr("PDF output requires a local file path"));
        return nullptr;
    }
    return std::make_unique<PdfFileDevice>(path, ticket.colorMode == QLatin1String("grayscale"));
}

QString localPath(const QString &pathOrUrl)
{
    if (pathOrUrl.isEmpty()) return {};
    const QUrl url(pathOrUrl);
    if (url.scheme().isEmpty()) return pathOrUrl;
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

bool runSynchronously(RichTextLayout &layout, const QVector<Sheet> &sheets, Device &device,
                      RichTextObjectRenderer *objects, QString *error)
{
    if (sheets.isEmpty()) {
        device.abort();
        return setError(error, tr("No pages are selected"));
    }
    for (int index = 0; index < sheets.size(); ++index) {
        if (!device.beginSheet(sheets.at(index), index, error)) {
            device.abort();
            return false;
        }
        paintSheet(device.painter(), layout, sheets.at(index), objects, false);
    }
    return device.finish(error);
}

} // namespace QindaTK::Print
