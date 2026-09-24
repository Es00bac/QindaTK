// SPDX-License-Identifier: LGPL-3.0-or-later
// Logical position <-> page geometry mapping and layout-aware navigation.
#include "rich_text_layout_p.h"
#include "rich_text_spans.h"

#include <cmath>

namespace QindaTK {
using namespace RichText;

QVector<PageLine> RichTextLayout::pageLines(int pageIndex)
{
    QVector<PageLine> result;
    if (!m_document || pageIndex < 0 || pageIndex >= pageCount()) return result;
    const Page &page = m_pages.at(pageIndex);
    const QPointF content = contentRect().topLeft();
    // AGENT-GUARD: build every table geometry of the page first. Building one
    // re-lays its cell paragraphs at that width, which would change metrics
    // that PageLines collected earlier still point to (a table continued in a
    // frame of another width on the same page).
    QVector<std::shared_ptr<const TableGeometry>> tables;
    for (const PageItem &item : page.items) {
        const Table *table = item.kind == PageItem::TableRows && !item.headerRepeat ? m_document->table(item.block) : nullptr;
        tables.append(table ? tableGeometry(*table, item.width) : nullptr);
    }
    for (int j = 0; j < page.items.size(); ++j) {
        const PageItem &item = page.items.at(j);
        if (item.kind == PageItem::Lines) {
            const Paragraph *p = m_document->paragraph(item.block);
            if (!p) continue;
            ParagraphMetrics &m = itemMetrics(*p, item);
            // AGENT-GUARD: pages are rebuilt when the document reports a change;
            // a query that runs earlier must not index lines the item no longer has.
            if (item.first + item.count > m.lines.size()) continue;
            const QPointF origin = content + QPointF(item.x, item.y - m.lines.at(item.first).y);
            for (int l = item.first; l < item.first + item.count; ++l) {
                const LineBox &box = m.lines.at(l);
                result.append({p, &m, l, QRectF(origin.x() + box.x, origin.y() + box.y, box.width, box.height),
                               origin, pageIndex, false});
            }
            continue;
        }
        if (!tables.at(j) || item.first + item.count > tables.at(j)->rows) continue;
        const TableGeometry &g = *tables.at(j);
        for (const TableGeometry::VisibleCell &visible : g.visibleCells(item)) {
            const TableGeometry::Cell &cell = *visible.cell;
            const qreal shift = cell.origin(visible.start);
            const QPointF cellOrigin = content + QPointF(item.x + cell.contentX,
                                                         item.y + visible.top + g.border + g.padding - shift);
            for (int l = visible.start; l < visible.end; ++l) {
                const auto &line = cell.lines.at(l);
                const Paragraph *p = m_document->paragraph(cell.paragraphs.at(line.paragraph));
                ParagraphMetrics &m = ensureParagraph(*p, cell.contentWidth);
                const QPointF origin = cellOrigin + QPointF(0, cell.paragraphTop.at(line.paragraph));
                const LineBox &box = m.lines.at(line.line);
                result.append({p, &m, line.line, QRectF(origin.x() + box.x, origin.y() + box.y, box.width, box.height),
                               origin, pageIndex, true});
            }
        }
    }
    return result;
}

static int lineForOffset(const ParagraphMetrics &m, int layoutOffset, Affinity affinity)
{
    int line = 0;
    for (int i = 0; i < m.lines.size(); ++i)
        if (m.lines.at(i).start <= layoutOffset) line = i;
    const LineBox &box = m.lines.at(line);
    if (affinity == Affinity::Upstream && line > 0 && layoutOffset == box.start) return line - 1;
    return line;
}

std::optional<PageLine> RichTextLayout::lineOf(const Position &position, int *layoutOffsetOut)
{
    if (!m_document) return std::nullopt;
    const Paragraph *p = m_document->paragraph(position.paragraph);
    if (!p) return std::nullopt;
    ensurePaginatedThrough(position);
    int line = -1;
    int lo = 0;
    const ParagraphMetrics *seen = nullptr;
    for (int page : pagesContaining(p)) {
        for (const PageLine &candidate : pageLines(page)) {
            if (candidate.paragraph != p) continue;
            // AGENT-GUARD: a paragraph continued in a region of another width
            // has one metrics per width, each with its own line indexes.
            if (candidate.metrics != seen) {
                seen = candidate.metrics;
                const ParagraphMetrics &m = *candidate.metrics;
                lo = layoutOffset(m, qBound(0, position.offset, int(p->text.size())));
                // The caret of an active composition sits inside the preedit text.
                if (m.preeditOffset >= 0 && position.offset == m.preeditOffset && m_preeditCursor >= 0)
                    lo = m.preeditOffset + m_preeditCursor;
                line = lineForOffset(m, lo, position.affinity);
            }
            if (candidate.line != line) continue;
            if (layoutOffsetOut) *layoutOffsetOut = lo;
            return candidate;
        }
    }
    return std::nullopt;
}

CaretBox RichTextLayout::caretBox(const Position &position)
{
    int lo = 0;
    const auto line = lineOf(position, &lo);
    if (!line) return {};
    QTextLayout *layout = shaped(*line->paragraph, *line->metrics);
    const qreal x = layout->lineAt(line->line).cursorToX(lo);
    return {line->page, QRectF(line->origin.x() + x, line->box.top(), 0, line->box.height()),
            line->metrics->rightToLeft};
}

int RichTextLayout::pageOfPosition(const Position &position)
{
    return caretBox(position).page;
}

Position RichTextLayout::positionInLine(const PageLine &line, qreal x)
{
    QTextLayout *layout = shaped(*line.paragraph, *line.metrics);
    const QTextLine textLine = layout->lineAt(line.line);
    const int lo = textLine.xToCursor(x - line.origin.x(), QTextLine::CursorBetweenCharacters);
    const LineBox &box = line.metrics->lines.at(line.line);
    int offset = modelOffset(*line.metrics, lo);
    offset = qBound(0, offset, int(line.paragraph->text.size()));
    const bool wrapped = line.line + 1 < line.metrics->lines.size();
    const bool atEnd = lo >= box.start + box.length;
    if (atEnd && wrapped && offset > 0 && line.paragraph->text.at(offset - 1) == QChar(LineBreakChar)) --offset;
    return {line.paragraph->id, offset, atEnd && wrapped ? Affinity::Upstream : Affinity::Downstream};
}

Position RichTextLayout::hitTest(int pageIndex, const QPointF &point)
{
    if (!m_document) return {};
    if (pageIndex < 0 || pageIndex >= pageCount()) return m_document->startPosition();
    const Page &page = m_pages.at(pageIndex);
    const QPointF content = contentRect().topLeft();
    for (const PageItem &item : page.items) {
        if (item.kind != PageItem::Float && item.kind != PageItem::PageObject) continue;
        if (!QRectF(content.x() + item.x, content.y() + item.y, item.width, item.height).contains(point)) continue;
        const Paragraph *p = m_document->paragraph(item.block);
        for (int i = 0; i < p->text.size(); ++i)
            if (p->text.at(i) == QChar(ObjectChar)
                && m_document->charFormat(Spans::formatAt(p->spans, i)).objectId == item.objectId)
                return {p->id, i};
    }
    const QVector<PageLine> lines = pageLines(pageIndex);
    if (lines.isEmpty()) return page.first.isValid() ? page.first : m_document->startPosition();
    const PageLine *best = nullptr;
    qreal bestScore = std::numeric_limits<qreal>::max();
    for (const PageLine &line : lines) {
        const qreal dy = point.y() < line.box.top() ? line.box.top() - point.y()
            : point.y() > line.box.bottom() ? point.y() - line.box.bottom() : 0;
        const qreal dx = point.x() < line.box.left() ? line.box.left() - point.x()
            : point.x() > line.box.right() ? point.x() - line.box.right() : 0;
        const qreal score = dy * 1000 + dx;
        if (score < bestScore) {
            bestScore = score;
            best = &line;
        }
    }
    return positionInLine(*best, point.x());
}

QVector<QRectF> RichTextLayout::selectionRects(int pageIndex, const Selection &selection)
{
    QVector<QRectF> rects;
    if (!m_document || selection.isCollapsed()) return rects;
    const bool ordered = m_document->compare(selection.anchor, selection.focus) <= 0;
    const Position start = ordered ? selection.anchor : selection.focus;
    const Position end = ordered ? selection.focus : selection.anchor;
    for (const PageLine &line : pageLines(pageIndex)) {
        const Position lineStart{line.paragraph->id, modelOffset(*line.metrics, line.metrics->lines.at(line.line).start)};
        const LineBox &box = line.metrics->lines.at(line.line);
        const Position lineEnd{line.paragraph->id, modelOffset(*line.metrics, box.start + box.length)};
        if (m_document->compare(lineEnd, start) < 0 || m_document->compare(lineStart, end) > 0) continue;
        if (m_document->compare(lineEnd, start) == 0 && !(lineStart == lineEnd)) continue;
        QTextLayout *layout = shaped(*line.paragraph, *line.metrics);
        const QTextLine textLine = layout->lineAt(line.line);
        const int from = m_document->compare(start, lineStart) > 0 ? layoutOffset(*line.metrics, start.offset) : box.start;
        const int to = m_document->compare(end, lineEnd) < 0 ? layoutOffset(*line.metrics, end.offset) : box.start + box.length;
        const qreal x0 = textLine.cursorToX(from);
        const qreal x1 = textLine.cursorToX(to);
        rects.append(QRectF(line.origin.x() + qMin(x0, x1), line.box.top(), qMax<qreal>(1, std::abs(x1 - x0)),
                            line.box.height()));
    }
    return rects;
}

std::optional<QRectF> RichTextLayout::objectRect(const QString &objectId, int *pageOut)
{
    if (!m_document || !m_document->object(objectId)) return std::nullopt;
    const QPointF content = contentRect().topLeft();
    for (const Paragraph *p : m_document->paragraphsInOrder()) {
        if (!p->text.contains(QChar(ObjectChar))) continue;
        for (int page : pagesContaining(p))
            for (const PageItem &item : m_pages.at(page).items)
                if (item.objectId == objectId) {
                    if (pageOut) *pageOut = page;
                    return QRectF(content.x() + item.x, content.y() + item.y, item.width, item.height);
                }
    }
    const RichObject *object = m_document->object(objectId);
    for (const Paragraph *p : m_document->paragraphsInOrder())
        for (int i = 0; i < p->text.size(); ++i) {
            if (p->text.at(i) != QChar(ObjectChar)
                || m_document->charFormat(Spans::formatAt(p->spans, i)).objectId != objectId)
                continue;
            int lo = 0;
            const auto line = lineOf({p->id, i}, &lo);
            if (!line || object->placement != Placement::Inline) return std::nullopt;
            QTextLayout *layout = shaped(*p, *line->metrics);
            const QTextLine textLine = layout->lineAt(line->line);
            const qreal x = qMin(textLine.cursorToX(lo), textLine.cursorToX(lo + 1));
            const qreal baseline = line->origin.y() + textLine.y() + textLine.ascent();
            const qreal h = object->heightPt * PxPerPt;
            if (pageOut) *pageOut = line->page;
            return QRectF(line->origin.x() + x, baseline - h, object->widthPt * PxPerPt, h);
        }
    return std::nullopt;
}

const ParagraphMetrics *RichTextLayout::metrics(const QUuid &paragraph)
{
    const auto found = m_metrics.find(paragraph);
    return found == m_metrics.end() ? nullptr : &found->second;
}

} // namespace QindaTK
