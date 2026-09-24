// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_document.h"
#include "rich_text_layout.h"
#include "rich_text_objects.h"

#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QUrl>

namespace QindaTK {

bool RichTextDocument::exportPdf(const QString &path)
{
    const QUrl url(path);
    if (path.isEmpty() || (!url.scheme().isEmpty() && !url.isLocalFile())) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("PDF output requires a local file path"));
        return false;
    }
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : path;
    RichTextLayout layout;
    layout.setDocument(this);
    layout.setMode(RichTextLayout::Mode::Paged);
    layout.paginateAll();
    if (!layout.paginationComplete() || layout.pageCount() == 0) {
        reject(ErrorCode::BackendFailure, QStringLiteral("Document pagination did not complete"));
        return false;
    }

    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) {
        reject(ErrorCode::BackendFailure, QStringLiteral("Cannot write PDF %1: %2").arg(localPath, file.errorString()));
        return false;
    }
    bool painted = false;
    {
        QPdfWriter writer(&file);
        writer.setResolution(96); // RichTextLayout paints at 96 px per inch.
        const RichText::PageSetup page = pageSetupValue();
        writer.setPageSize(QPageSize(QSizeF(page.widthPt, page.heightPt), QPageSize::Point));
        writer.setPageMargins(QMarginsF(), QPageLayout::Point);
        QPainter painter;
        if (painter.begin(&writer)) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            RichTextImageRenderer objects;
            RichText::PaintContext context;
            context.ink = Qt::black;
            context.link = QColor(QStringLiteral("#0645ad"));
            context.border = QColor(QStringLiteral("#888888"));
            context.objectFill = QColor(QStringLiteral("#f0f0f0"));
            context.objectText = Qt::black;
            context.marker = Qt::black;
            context.objects = &objects;
            painted = true;
            for (int pageIndex = 0; pageIndex < layout.pageCount(); ++pageIndex) {
                if (pageIndex > 0 && !writer.newPage()) { painted = false; break; }
                layout.paintPage(&painter, pageIndex, context);
            }
            painted = painter.end() && painted;
        }
    }
    if (!painted || !file.commit()) {
        reject(ErrorCode::BackendFailure, QStringLiteral("PDF export failed for %1").arg(localPath));
        return false;
    }
    return true;
}

} // namespace QindaTK
