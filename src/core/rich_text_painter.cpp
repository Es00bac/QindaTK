// SPDX-License-Identifier: LGPL-3.0-or-later
// Painting of laid-out pages. Theme colours (ink, links, selection) are
// draw-time formats, so a theme change never re-shapes text.
#include "rich_text_layout_p.h"
#include "rich_text_objects.h"
#include "rich_text_shaping_p.h"
#include "rich_text_spans.h"

#include <QPainter>

namespace QindaTK {
using namespace RichText;

QList<QTextLayout::FormatRange> RichTextLayout::decorations(const Paragraph &p, const ParagraphMetrics &m,
                                                             const PaintContext &context) const
{
    QList<QTextLayout::FormatRange> ranges;
    int start = 0;
    for (const Span &span : p.spans) {
        const CharFormat &format = m_document->charFormat(span.format);
        if (!format.href.isEmpty() && !format.color.isValid()) {
            QTextCharFormat link;
            link.setForeground(context.link);
            link.setFontUnderline(true);
            const int from = layoutOffset(m, start);
            ranges.append({from, layoutOffset(m, start + span.length) - from, link});
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
        if (context.objects) context.objects->paint(painter, rect, *object, context);
        else RichTextImageRenderer::paintPlaceholder(painter, rect, *object, context, false);
    }
}

void RichTextLayout::paintLines(QPainter *painter, const PageItem &item, const PaintContext &context)
{
    const Paragraph *p = m_document->paragraph(item.block);
    if (!p) return;
    ParagraphMetrics &m = ensureParagraph(*p, itemWidth(), item.exclusions);
    const QPointF content = contentRect().topLeft();
    const QPointF origin = content + QPointF(0, item.y - m.lines.at(item.first).y);
    // A strict vertical clip keeps lines that belong to another page out.
    const QRectF clip(content.x() - 4096, content.y() + item.y + 0.25, itemWidth() + 8192, item.height - 0.5);
    paintParagraph(painter, *p, m, origin, clip, item.first, item.count, context);
}

void RichTextLayout::paintTable(QPainter *painter, const PageItem &item, const PaintContext &context)
{
    const Table *table = m_document->table(item.block);
    if (!table) return;
    const auto &g = tableGeometry(*table);
    const QPointF content = contentRect().topLeft();
    const qreal b = g.border;
    const qreal left = content.x();
    const qreal right = content.x() + g.width;
    const bool split = !item.cellFirstLine.isEmpty() || item.rowClip > 0;
    qreal rowTop = content.y() + item.y;
    for (int r = item.first; r < item.first + item.count; ++r) {
        const qreal rowHeight = split ? item.height - b : g.rowHeight.at(r);
        for (int c = 0; c < g.cells.at(r).size(); ++c) {
            const auto &cell = g.cells.at(r).at(c);
            const QColor background = table->rows.at(r).cells.at(c).background;
            const QRectF box(left + g.columnX.at(c) + b, rowTop + b, g.columnWidth.at(c) - b, rowHeight - b);
            if (background.isValid()) painter->fillRect(box, background);
            const int firstLine = item.cellFirstLine.value(c, 0);
            const qreal shift = firstLine < cell.lines.size() ? cell.lines.at(firstLine).top : 0;
            const qreal contentTop = rowTop + b + g.padding;
            const qreal visible = item.rowClip > 0 ? item.rowClip : cell.contentHeight - shift;
            const QRectF clip(left + cell.contentX - 2, contentTop + 0.25, cell.contentWidth + 4, visible - 0.5);
            const auto &paragraphs = table->rows.at(r).cells.at(c).paragraphs;
            for (int i = 0; i < paragraphs.size(); ++i) {
                ParagraphMetrics &m = ensureParagraph(*paragraphs.at(i), cell.contentWidth);
                const QPointF origin(left + cell.contentX, contentTop + cell.paragraphTop.at(i) - shift);
                if (origin.y() + m.height < clip.top() || origin.y() > clip.bottom()) continue;
                paintParagraph(painter, *paragraphs.at(i), m, origin, clip, 0, int(m.lines.size()), context);
            }
        }
        if (b > 0) {
            painter->fillRect(QRectF(left, rowTop, right - left, b), context.border);
            for (int c = 0; c < g.columnX.size(); ++c)
                painter->fillRect(QRectF(left + g.columnX.at(c), rowTop, b, rowHeight), context.border);
            painter->fillRect(QRectF(right - b, rowTop, b, rowHeight), context.border);
        }
        rowTop += rowHeight;
    }
    if (b > 0 && !item.headerRepeat) painter->fillRect(QRectF(left, rowTop, right - left, b), context.border);
}

void RichTextLayout::paintPage(QPainter *painter, int pageIndex, const PaintContext &context, const QRectF &clip)
{
    if (!m_document || pageIndex < 0 || pageIndex >= pageCount()) return;
    const Page &page = m_pages.at(pageIndex);
    const QPointF content = contentRect().topLeft();
    painter->save();
    painter->setPen(context.ink);
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
            if (const RichObject *object = m_document->object(item.objectId)) {
                if (context.objects) context.objects->paint(painter, itemRect, *object, context);
                else RichTextImageRenderer::paintPlaceholder(painter, itemRect, *object, context, false);
            }
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
