// SPDX-License-Identifier: LGPL-3.0-or-later
#include "print_output.h"
#include "rich_text_document.h"
#include "rich_text_layout.h"
#include "rich_text_objects.h"

namespace QindaTK {

// AGENT-NOTE (D-130): exportPdf is the default ticket of PrintAdapter's "pdf"
// destination (document paper, every page, actual size); it paints through
// Print::paintSheet like the preview and printer jobs, synchronously.
bool RichTextDocument::exportPdf(const QString &path)
{
    Print::Ticket ticket;
    ticket.outputPath = path;
    ticket.paperId = QStringLiteral("document");
    ticket.scaleMode = QStringLiteral("actual");
    if (Print::localPath(path).isEmpty()) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("PDF output requires a local file path"));
        return false;
    }
    RichTextLayout layout;
    layout.setDocument(this);
    layout.setMode(RichTextLayout::Mode::Paged);
    layout.paginateAll();
    if (!layout.paginationComplete() || layout.pageCount() == 0) {
        reject(ErrorCode::BackendFailure, QStringLiteral("Document pagination did not complete"));
        return false;
    }
    const Print::Paper paper = Print::standardPapers().first(); // the document paper
    const QVector<Print::Sheet> sheets = Print::planSheets(m_page, layout.pageCount(), ticket, paper);
    Print::PdfFileBackend backend;
    QString error;
    std::unique_ptr<Print::Device> device = backend.open(ticket, Print::PdfFileBackend::capabilities(), &error);
    RichTextImageRenderer objects;
    if (!device || !Print::runSynchronously(layout, sheets, *device, &objects, &error)) {
        reject(ErrorCode::BackendFailure, error.isEmpty() ? QStringLiteral("PDF export failed for %1").arg(path) : error);
        return false;
    }
    return true;
}

} // namespace QindaTK
