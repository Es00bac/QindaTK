// SPDX-License-Identifier: LGPL-3.0-or-later
// Painting of laid-out pages. Theme colours (ink, links, selection) are
// draw-time formats, so a theme change never re-shapes text.
#include "rich_text_equation.h"
#include "rich_text_layout_p.h"
#include "rich_text_objects.h"
#include "rich_text_shaping_p.h"
#include "rich_text_spans.h"

#include <QPainter>

namespace QindaTK {
using namespace RichText;

namespace {

// Equations draw themselves from their source (D-114); other objects go
// through the host's renderer or the labelled placeholder.
void paintObject(QPainter *painter, const QRectF &rect, const RichObject &object, const PaintContext &context)
{
    const QColor ink = context.ink.isValid() ? context.ink : painter->pen().color();
    if (object.kind == u"equation" && Equation::paintObject(painter, rect, object, ink)) return;
    if (context.objects) context.objects->paint(painter, rect, object, context);
    else RichTextImageRenderer::paintPlaceholder(painter, rect, object, context, false);
}

} // namespace

QList<QTextLayout::FormatRange> RichTextLayout::decorations(const Paragraph &p, const ParagraphMetrics &m,
                                                             const PaintContext &context) const
{
    QList<QTextLayout::FormatRange> ranges;
    int start = 0;
    for (const Span &span : p.spans) {
        const CharFormat format = m_document->effectiveCharFormat(m_document->charFormat(span.format), p.format);
        if (!format.href.isEmpty() && !format.color.isValid()) {
            QTextCharFormat link;
            link.setForeground(context.link);
            link.setFontUnderline(true);
            const int from = layoutOffset(m, start);
            ranges.append({from, layoutOffset(m, start + span.length) - from, link});
        }
        // Review marks (docs/references-review.md#review): shaping already
        // underlines insertions and strikes deletions; colours are draw-time.
        bool openComment = false;
        for (const QString &id : format.commentIds) {
            const auto thread = m_document->annotations().threads.constFind(id);
            openComment = openComment || (thread != m_document->annotations().threads.constEnd() && !thread->resolved);
        }
        QTextCharFormat review;
        bool marked = false;
        if (!format.deletionId.isEmpty() && context.deletion.isValid()) {
            review.setForeground(context.deletion);
            marked = true;
        } else if (!format.insertionId.isEmpty() && context.insertion.isValid()) {
            review.setForeground(context.insertion);
            marked = true;
        }
        if (openComment && context.comment.isValid()) {
            review.setBackground(context.comment);
            marked = true;
        }
        if (marked) {
            const int from = layoutOffset(m, start);
            ranges.append({from, layoutOffset(m, start + span.length) - from, review});
        }
        start += span.length;
    }
    if (!context.hasSelection || context.selection.isCollapsed()) return ranges;
    const bool ordered = m_document->compare(context.selection.anchor, context.selection.focus) <= 0;
    const Position first = ordered ? context.selection.anchor : context.selection.focus;
    const Position last = ordered ? context.selection.focus : context.selection.anchor;
    const Position here{p.id, 0};
    const Position there{p.id, int(p.text.size())};
    if (m_document->compare(there, first) < 0 || m_document->compare(here, last) > 0) return ranges;
    const int from = first.paragraph == p.id ? first.offset : 0;
    const int to = last.paragraph == p.id ? last.offset : int(p.text.size());
    if (to <= from) return ranges;
    QTextCharFormat selected;
    selected.setBackground(context.selectionBackground);
    if (context.selectionText.isValid()) selected.setForeground(context.selectionText);
    const int lo = layoutOffset(m, from);
    ranges.append({lo, layoutOffset(m, to) - lo, selected});
    return ranges;
}

void RichTextLayout::paintParagraph(QPainter *painter, const Paragraph &p, ParagraphMetrics &m,
                                    const QPointF &origin, const QRectF &clip, int firstLine,
                                    int lineCount, const PaintContext &context)
{
    QTextLayout *layout = shaped(p, m);
    layout->draw(painter, origin, decorations(p, m, context), clip);
    const int lastLine = firstLine + lineCount - 1;
    // Selected paragraph mark: a small block after the last line's text.
    if (context.hasSelection && !context.selection.isCollapsed() && lastLine == m.lines.size() - 1) {
        const bool ordered = m_document->compare(context.selection.anchor, context.selection.focus) <= 0;
        const Position first = ordered ? context.selection.anchor : context.selection.focus;
        const Position last = ordered ? context.selection.focus : context.selection.anchor;
        const Position end{p.id, int(p.text.size())};
        if (m_document->compare(first, end) <= 0 && m_document->compare(end, last) < 0) {
            const QTextLine line = layout->lineAt(lastLine);
            const qreal x = line.cursorToX(line.textStart() + line.textLength());
            const LineBox &box = m.lines.at(lastLine);
            const qreal w = qMax<qreal>(3, box.height * 0.3);
            painter->fillRect(QRectF(origin.x() + (m.rightToLeft ? x - w : x), origin.y() + box.y, w, box.height),
                              context.selectionBackground);
        }
    }
    // List marker in the hanging indent of the first line.
    const QString marker = m_markers.value(p.id);
    if (firstLine == 0 && !marker.isEmpty()) {
        QTextLayout markerLayout(marker, Shaping::resolvedFont(*m_document, m_document->charFormat(p.markFormat), p.format),
                                 Shaping::layoutDevice());
        markerLayout.beginLayout();
        QTextLine markerLine = markerLayout.createLine();
        markerLine.setLineWidth(1e5);
        markerLayout.endLayout();
        const QTextLine first = layout->lineAt(0);
        const qreal gap = 6 * PxPerPt;
        const qreal w = markerLine.naturalTextWidth();
        const qreal x = m.rightToLeft ? m.width - m.indentStart + gap : m.indentStart - gap - w;
        const qreal baseline = first.y() + first.ascent();
        markerLine.setPosition(QPointF(0, 0));
        painter->save();
        painter->setPen(context.marker.isValid() ? context.marker : context.ink);
        markerLayout.draw(painter, origin + QPointF(x, baseline - markerLine.ascent()));
        painter->restore();
    }
    // Inline objects sit on the baseline of their line.
    for (int i = 0; i < p.text.size(); ++i) {
        if (p.text.at(i) != QChar(ObjectChar)) continue;
        const RichObject *object = m_document->object(m_document->charFormat(Spans::formatAt(p.spans, i)).objectId);
        if (!object || object->placement != Placement::Inline) continue;
        const int lo = layoutOffset(m, i);
        int line = firstLine;
        while (line + 1 <= lastLine && m.lines.at(line + 1).start <= lo) ++line;
        const LineBox &box = m.lines.at(line);
        if (lo < box.start || lo >= box.start + box.length) continue;
        const QTextLine textLine = layout->lineAt(line);
        const qreal x = qMin(textLine.cursorToX(lo), textLine.cursorToX(lo + 1));
        const qreal h = object->heightPt * PxPerPt;
        const QRectF rect(origin.x() + x, origin.y() + textLine.y() + textLine.ascent() - h, object->widthPt * PxPerPt, h);
        paintObject(painter, rect, *object, context);
    }
}

void RichTextLayout::paintLines(QPainter *painter, const PageItem &item, const PaintContext &context)
{
    const Paragraph *p = m_document->paragraph(item.block);
    if (!p) return;
    ParagraphMetrics &m = itemMetrics(*p, item);
    if (item.first + item.count > m.lines.size()) return; // pages not yet rebuilt
    const QPointF content = contentRect().topLeft();
    const QPointF origin = content + QPointF(item.x, item.y - m.lines.at(item.first).y);
    // A strict vertical clip keeps lines that belong to another page or region out.
    const QRectF clip(content.x() + item.x - 4096, content.y() + item.y + 0.25, item.width + 8192, item.height - 0.5);
    // AGENT-NOTE: QTextLayout::draw's clip argument only skips lines, and it
    // still draws the line just above the rectangle, so a paragraph continued
    // in the next column or frame repeated the last line shown before it. The
    // painter clips the item's band as well (tst_rich_text_frames, grabs).
    painter->save();
    painter->setClipRect(clip, Qt::IntersectClip);
    paintParagraph(painter, *p, m, origin, clip, item.first, item.count, context);
    painter->restore();
}

void RichTextLayout::paintTable(QPainter *painter, const PageItem &item, const PaintContext &context)
{
    const Table *table = m_document->table(item.block);
    if (!table) return;
    const auto geometry = tableGeometry(*table, item.width);
    const TableGeometry &g = *geometry;
    if (item.first + item.count > g.rows) return; // pages not yet rebuilt
    const QPointF content = contentRect().topLeft();
    const qreal b = g.border;
    const qreal left = content.x() + item.x;
    const qreal top = content.y() + item.y;
    qreal rows = 0;
    for (qreal height : item.rowHeights) rows += height;
    // AGENT-NOTE: borders are collapsed per cell box (top and start edges;
    // the table draws its end and bottom edges), so a merged cell has no
    // inner grid lines (docs/publishing.md#merged-cells).
    for (const TableGeometry::VisibleCell &visible : g.visibleCells(item)) {
        const TableGeometry::Cell &cell = *visible.cell;
        const qreal x0 = left + g.columnX.at(cell.column);
        const qreal x1 = left + g.columnRight(cell.column + cell.columnSpan);
        const qreal y0 = top + visible.top;
        if (cell.background.isValid())
            painter->fillRect(QRectF(x0 + b, y0 + b, x1 - x0 - b, visible.height - b), cell.background);
        if (visible.end > visible.start) {
            const qreal shift = cell.origin(visible.start);
            const qreal contentTop = y0 + b + g.padding;
            const qreal shown = cell.lines.at(visible.end - 1).bottom - shift;
            const QRectF clip(left + cell.contentX - 2, contentTop + 0.25, cell.contentWidth + 4, shown - 0.5);
            painter->save();
            painter->setClipRect(clip, Qt::IntersectClip); // see paintLines
            // Each paragraph paints only its lines in this box (list markers too).
            for (int l = visible.start; l < visible.end;) {
                const int index = cell.lines.at(l).paragraph;
                int count = 0;
                while (l + count < visible.end && cell.lines.at(l + count).paragraph == index) ++count;
                const Paragraph *p = m_document->paragraph(cell.paragraphs.at(index));
                ParagraphMetrics &m = ensureParagraph(*p, cell.contentWidth);
                const QPointF origin(left + cell.contentX, contentTop + cell.paragraphTop.at(index) - shift);
                paintParagraph(painter, *p, m, origin, clip, cell.lines.at(l).line, count, context);
                l += count;
            }
            painter->restore();
        }
        if (b > 0) {
            painter->fillRect(QRectF(x0, y0, x1 - x0, b), context.border);
            painter->fillRect(QRectF(x0, y0, b, visible.height), context.border);
        }
    }
    if (b > 0) {
        painter->fillRect(QRectF(left + g.inner, top, b, rows), context.border);
        if (!item.headerRepeat) painter->fillRect(QRectF(left, top + rows, g.width, b), context.border);
    }
}

void RichTextLayout::paintPage(QPainter *painter, int pageIndex, const PaintContext &context, const QRectF &clip)
{
    if (!m_document || pageIndex < 0 || pageIndex >= pageCount()) return;
    const Page &page = m_pages.at(pageIndex);
    const QPointF content = contentRect().topLeft();
    painter->save();
    painter->setPen(context.ink);
    paintFrames(painter, pageIndex, context);
    for (const PageItem &item : page.items) {
        const QRectF itemRect(content.x() + item.x, content.y() + item.y, item.width, item.height);
        if (!clip.isNull() && !clip.intersects(itemRect.adjusted(-64, -2, 64, 2))) continue;
        switch (item.kind) {
        case PageItem::Lines:
            paintLines(painter, item, context);
            break;
        case PageItem::TableRows:
            paintTable(painter, item, context);
            break;
        case PageItem::Float:
        case PageItem::PageObject:
            if (const RichObject *object = m_document->object(item.objectId))
                paintObject(painter, itemRect, *object, context);
            break;
        }
    }
    if (context.showCaret) {
        const CaretBox caret = caretBox(context.caret);
        if (caret.page == pageIndex)
            painter->fillRect(QRectF(caret.rect.x() - context.caretWidth / 2, caret.rect.y(), context.caretWidth,
                                     caret.rect.height()), context.caretColor);
    }
    painter->restore();
}

} // namespace QindaTK
