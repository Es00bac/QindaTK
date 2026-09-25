// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_ticket.h"
#include "rich_text_types.h"
#include <QImage>
#include <QPageLayout>
#include <QRectF>
#include <memory>

class QPainter;

namespace QindaTK {
class RichTextLayout;
class RichTextObjectRenderer;

namespace RichText { struct PaintContext; }

// Page output shared by preview, file export and printers (WP-053, D-130).
namespace Print {

inline constexpr double CropMarkPt = 18.0;   // slug around the bleed that holds crop marks
inline constexpr double DeviceDpi = 96.0;    // every device paints in RichTextLayout px

// One output sheet: where a document page lands on the paper fed to a device.
struct Sheet {
    int sourcePage = 0;          // zero-based layout page
    QSizeF sizePt;               // the sheet as fed to the device
    QRectF trimPt;               // the page's trim box on the sheet
    double scale = 1.0;          // document points to sheet points
    double bleedPt = 0;          // bleed around the trim box, sheet points
    double markPt = 0;           // crop-mark length, sheet points (0: no marks)
    bool clipped = false;        // the scaled page and its bleed overrun the sheet
    friend bool operator==(const Sheet &, const Sheet &) = default;
};

// AGENT-CONTRACT (D-130, docs/printing.md#one-output-description): preview
// images, PDF files and printer jobs all take their geometry from sheetFor()
// and their pixels from paintSheet(); a device only supplies a painter in
// 1/96-inch units. A second painting path would let preview and output drift.
Sheet sheetFor(const RichText::PageSetup &page, int sourcePage, const Ticket &ticket, const Paper &paper);
QVector<Sheet> planSheets(const RichText::PageSetup &page, int pageCount, const Ticket &ticket, const Paper &paper);
QVariantMap sheetToMap(const Sheet &sheet);
// Pixel size of a sheet rendered at `zoom` (1: 96 pixels per inch).
QSize sheetPixelSize(const Sheet &sheet, double zoom);

// Output colours: black ink, no selection, caret or editing guides.
RichText::PaintContext outputContext(RichTextObjectRenderer *objects);
// Paints `sheet` with the painter in sheet units of 1/96 inch; `paper` fills
// the sheet white first (preview; devices start on blank paper).
void paintSheet(QPainter *painter, RichTextLayout &layout, const Sheet &sheet,
                RichTextObjectRenderer *objects, bool paper);
// The preview image of a sheet; grayscale output is previewed in gray.
QImage renderSheet(RichTextLayout &layout, const Sheet &sheet, double zoom, bool grayscale,
                   RichTextObjectRenderer *objects);

// One open output (file or spooled job). Sheets arrive in order; finish()
// completes the output and abort() discards it.
class Device {
public:
    virtual ~Device() = default;
    // Starts sheet `index` (0 = the first) at the sheet's size.
    virtual bool beginSheet(const Sheet &sheet, int index, QString *error) = 0;
    virtual QPainter *painter() = 0;
    virtual bool finish(QString *error) = 0;
    virtual void abort() = 0;
    // Sheets that already reached paper or disk and survive abort() (0 for
    // devices that commit at finish, like the PDF file and CUPS spool).
    virtual int deliveredSheets() const { return 0; }
};

// A source of destinations: the built-in PDF writer, or the platform's
// printers (NativePrintBackend in qindatk_widgets).
class Backend {
public:
    virtual ~Backend() = default;
    virtual QVector<Capabilities> destinations() = 0;
    virtual std::unique_ptr<Device> open(const Ticket &ticket, const Capabilities &destination, QString *error) = 0;
};
using BackendPtr = std::shared_ptr<Backend>;

// PDF files through QPdfWriter and QSaveFile: the file appears only when the
// job completes. Destination ID "pdf".
class PdfFileBackend final : public Backend {
public:
    static Capabilities capabilities();
    QVector<Capabilities> destinations() override { return {capabilities()}; }
    std::unique_ptr<Device> open(const Ticket &ticket, const Capabilities &destination, QString *error) override;
};

// A zero-margin page layout of exactly `sizePt`, landscape when wider than tall.
QPageLayout pageLayoutFor(const QSizeF &sizePt);
// A local file path from a path or file: URL; empty for any other URL.
QString localPath(const QString &pathOrUrl);
// Paints every sheet through `device` and finishes it; aborts on failure.
bool runSynchronously(RichTextLayout &layout, const QVector<Sheet> &sheets, Device &device,
                      RichTextObjectRenderer *objects, QString *error);

} // namespace Print
} // namespace QindaTK
