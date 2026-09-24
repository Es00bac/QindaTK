// SPDX-License-Identifier: LGPL-3.0-or-later
// RichTextArea and publishing structure (WP-051): style targets, text frames
// and the caret's structural location for keyboard and assistive users.
#include "rich_text_area.h"

namespace QindaTK {
using namespace RichText;

QStringList RichTextArea::selectedParagraphIds() const
{
    QStringList ids;
    if (!m_document) return ids;
    const Paragraph *a = m_document->paragraph(m_anchor.position.paragraph);
    const Paragraph *b = m_document->paragraph(m_focus.position.paragraph);
    if (!a || !b) return ids;
    const auto &flat = m_document->paragraphsInOrder();
    const int from = qMin(a->flatIndex, b->flatIndex);
    const int to = qMax(a->flatIndex, b->flatIndex);
    for (int i = from; i <= to && i < flat.size(); ++i) ids.append(flat.at(i)->id.toString(QUuid::WithoutBraces));
    return ids;
}

QString RichTextArea::caretFrameId() const
{
    // AGENT-GUARD: a getter never paginates. caretBox paginates only up to a
    // position that is not laid out yet, so isLaidOut comes first (as in
    // cursorRectangle); an unlaid caret has no frame yet. The layout always
    // handles a document change before this area signals (setDocument).
    if (!m_document || !m_layout->frameMode() || !m_layout->isLaidOut(m_focus.position)) return {};
    const CaretBox caret = m_layout->caretBox(m_focus.position);
    if (caret.page < 0) return {};
    const QPointF centre = caret.rect.center() - m_layout->contentRect().topLeft();
    for (const Region &region : m_layout->regions(caret.page))
        if (region.rect.adjusted(-1, -1, 1, 1).contains(centre)) return region.frameId;
    return {};
}

QString RichTextArea::caretContext() const
{
    const Paragraph *p = m_document ? m_document->paragraph(m_focus.position.paragraph) : nullptr;
    if (!p) return {};
    QStringList parts;
    const QString frame = caretFrameId();
    if (!frame.isEmpty()) {
        const QStringList story = m_document->frameSet().storyChain();
        parts.append(tr("Frame %1 of %2").arg(story.indexOf(frame) + 1).arg(story.size()));
    }
    m_document->paragraphsInOrder(); // current row and cell of table paragraphs
    if (p->table) {
        const TableCell &cell = p->table->rows.at(p->row).cells.at(p->cell);
        QString where = tr("Table row %1, column %2").arg(p->row + 1).arg(p->cell + 1);
        if (cell.rowSpan > 1 || cell.columnSpan > 1)
            where += u' ' + tr("(merged: %1 rows, %2 columns)").arg(cell.rowSpan).arg(cell.columnSpan);
        parts.append(where);
    }
    if (const NamedStyle *style = m_document->style(p->format.styleId)) parts.append(tr("Style %1").arg(style->name));
    return parts.join(QStringLiteral(", "));
}

QString RichTextArea::frameAt(qreal itemX, qreal itemY) const
{
    if (!m_document || !m_layout->frameMode() || pageCount() == 0) return {};
    const int page = pageAtY(itemY);
    return m_layout->frameAt(page, (QPointF(itemX, itemY) - pageOrigin(page)) / m_zoom);
}

bool RichTextArea::goToFrame(const QString &frameId)
{
    const TextFrame *frame = m_document ? m_document->frameSet().frame(frameId) : nullptr;
    if (!frame || !paged()) return false;
    if (const auto position = m_layout->framePosition(frameId)) {
        setSelection({*position, *position});
        return true;
    }
    return goToPage(frame->page);
}

} // namespace QindaTK
