// SPDX-License-Identifier: LGPL-3.0-or-later
// Page regions of RichTextLayout (WP-051): text columns of the page content
// box, or the columns of the linked text frames the story flows through.
#include "rich_text_layout_p.h"

#include <QPainter>
#include <limits>

namespace QindaTK {
using namespace RichText;

bool RichTextLayout::frameMode() const
{
    return m_mode == Mode::Paged && m_document && !m_document->frameSet().frames.isEmpty();
}

QVector<Region> RichTextLayout::regions(int page) const
{
    QVector<Region> result;
    if (!m_document) return result;
    if (m_mode == Mode::Continuous) {
        result.append({QRectF(0, 0, m_flowWidth, std::numeric_limits<qreal>::infinity()), {}});
        return result;
    }
    if (!frameMode()) {
        const PageSetup setup = m_document->pageSetupValue();
        const QRectF content = contentRect();
        const int columns = qMax(1, setup.columns);
        const qreal gap = columns > 1 ? setup.columnGapPt * PxPerPt : 0;
        const qreal width = (content.width() - (columns - 1) * gap) / columns;
        for (int c = 0; c < columns; ++c)
            result.append({QRectF(c * (width + gap), 0, width, content.height()), {}});
        return result;
    }
    // AGENT-CONTRACT (docs/publishing.md#text-frames): the story visits the
    // frames of its thread in order; FrameSet validation keeps a thread from
    // going back a page, so the pages are filled one after the other.
    const FrameSet &frames = m_document->frameSet();
    for (const QString &id : frames.storyChain()) {
        const TextFrame *f = frames.frame(id);
        if (f->page != page) continue;
        const qreal inset = f->insetPt * PxPerPt;
        const qreal gap = f->gutterPt * PxPerPt;
        const qreal width = (f->widthPt * PxPerPt - 2 * inset - (f->columns - 1) * gap) / f->columns;
        for (int c = 0; c < f->columns; ++c)
            result.append({QRectF(f->xPt * PxPerPt + inset + c * (width + gap), f->yPt * PxPerPt + inset,
                                  width, f->heightPt * PxPerPt - 2 * inset), f->id});
    }
    return result;
}

bool RichTextLayout::buildPage(int index, const FlowState &start, Page *page, FlowState *next)
{
    *page = Page{};
    page->start = start;
    FlowState state = start;
    const bool paged = m_mode == Mode::Paged;
    const auto &blocks = m_document->blocks();
    bool more = state.block < blocks.size();
    for (const Region &region : regions(index)) {
        if (!more) break;
        FlowBuilder builder(*this, *m_document, region.rect.width(), region.rect.height(), paged);
        Page part;
        FlowState after;
        more = builder.build(state, &part, &after);
        for (PageItem item : std::as_const(part.items)) {
            // Page objects keep their place in the content box.
            if (item.kind != PageItem::PageObject) {
                item.x += region.rect.x();
                item.y += region.rect.y();
            }
            item.frameId = region.frameId;
            page->items.append(item);
        }
        page->overflow = page->overflow || part.overflow;
        state = after;
        // A page break ends the page, not only the column or frame.
        if (more && paged && !page->items.isEmpty() && state.line == 0 && state.row == 0
            && state.cellLine.isEmpty()) {
            const Paragraph *p = blocks.value(state.block).paragraph.get();
            if (p && p->format.pageBreakBefore) break;
        }
    }
    *next = state;
    // A frame layout has exactly the frames' pages; the rest is overset.
    if (frameMode()) return index + 1 < m_document->frameSet().pageCount();
    return more;
}

bool RichTextLayout::overset() const
{
    return frameMode() && m_complete && m_frontier.block < m_document->blocks().size();
}

int RichTextLayout::oversetCharacters()
{
    if (!overset()) return 0;
    const auto &blocks = m_document->blocks();
    qint64 count = 0;
    for (int b = m_frontier.block; b < blocks.size(); ++b) {
        const Block &block = blocks.at(b);
        if (const Paragraph *p = block.paragraph.get()) {
            int from = 0;
            if (b == m_frontier.block && m_frontier.line > 0 && !m_frontier.widths.isEmpty()) {
                // The metrics the paragraph's placed lines came from (placeParagraph).
                QVector<WidthBreak> widths = m_frontier.widths;
                bool uniform = true;
                for (const WidthBreak &w : std::as_const(widths))
                    uniform = uniform && qFuzzyCompare(w.width + 1, widths.first().width + 1);
                const qreal width = widths.first().width;
                if (uniform) widths.clear();
                const ParagraphMetrics &m = ensureParagraph(*p, width, m_frontier.exclusions, false, widths);
                if (m_frontier.line < m.lines.size()) from = modelOffset(m, m.lines.at(m_frontier.line).start);
                else from = int(p->text.size());
            }
            count += p->text.size() - from;
            continue;
        }
        // Tables count whole cells: every cell not finished before the overset starts.
        const Table &table = *block.table;
        TableGridMap map;
        mapTableGrid(table, &map);
        const int firstRow = b == m_frontier.block ? m_frontier.row : 0;
        for (int r = 0; r < map.rows; ++r)
            for (int c = 0; c < map.columns; ++c) {
                const TableCell &cell = table.rows.at(r).cells.at(c);
                if (cell.paragraphs.isEmpty() || r + cell.rowSpan - 1 < firstRow) continue;
                for (const ParagraphPtr &p : cell.paragraphs) count += p->text.size();
            }
    }
    return int(qMin<qint64>(count, std::numeric_limits<int>::max()));
}

QString RichTextLayout::frameAt(int page, const QPointF &point) const
{
    if (!frameMode()) return {};
    const FrameSet &frames = m_document->frameSet();
    // The last created frame is drawn on top, so it wins.
    for (qsizetype i = frames.frames.size() - 1; i >= 0; --i) {
        const TextFrame &f = frames.frames.at(i);
        if (f.page == page && QRectF(f.xPt * PxPerPt, f.yPt * PxPerPt, f.widthPt * PxPerPt, f.heightPt * PxPerPt).contains(point))
            return f.id;
    }
    return {};
}

std::optional<Position> RichTextLayout::framePosition(const QString &frameId)
{
    const TextFrame *frame = frameMode() ? m_document->frameSet().frame(frameId) : nullptr;
    if (!frame) return std::nullopt;
    ensurePaginated(frame->page);
    if (frame->page >= pageCount()) return std::nullopt;
    for (const PageItem &item : m_pages.at(frame->page).items) {
        if (item.frameId != frameId) continue;
        if (item.kind == PageItem::Lines) {
            const Paragraph *p = m_document->paragraph(item.block);
            const ParagraphMetrics &m = itemMetrics(*p, item);
            return Position{p->id, modelOffset(m, m.lines.at(item.first).start)};
        }
        if (item.kind != PageItem::TableRows || item.headerRepeat) continue;
        const auto g = tableGeometry(*m_document->table(item.block), item.width);
        for (const auto &visible : g->visibleCells(item)) {
            if (visible.start >= visible.end) continue;
            const auto &line = visible.cell->lines.at(visible.start);
            const Paragraph *p = m_document->paragraph(visible.cell->paragraphs.at(line.paragraph));
            const ParagraphMetrics &m = ensureParagraph(*p, visible.cell->contentWidth);
            return Position{p->id, modelOffset(m, m.lines.at(line.line).start)};
        }
    }
    return std::nullopt;
}

void RichTextLayout::paintFrames(QPainter *painter, int pageIndex, const PaintContext &context)
{
    if (!context.guides || !frameMode()) return;
    const FrameSet &frames = m_document->frameSet();
    const QStringList story = frames.storyChain();
    const qreal w = qMax<qreal>(0.5, context.caretWidth);
    for (const TextFrame &f : frames.frames) {
        if (f.page != pageIndex) continue;
        const QRectF r(f.xPt * PxPerPt, f.yPt * PxPerPt, f.widthPt * PxPerPt, f.heightPt * PxPerPt);
        const QColor color = story.contains(f.id) && context.frameGuide.isValid() ? context.frameGuide : context.border;
        painter->fillRect(QRectF(r.left(), r.top(), r.width(), w), color);
        painter->fillRect(QRectF(r.left(), r.bottom() - w, r.width(), w), color);
        painter->fillRect(QRectF(r.left(), r.top(), w, r.height()), color);
        painter->fillRect(QRectF(r.right() - w, r.top(), w, r.height()), color);
        // Story text that no frame holds is marked on the thread's last frame.
        if (!story.isEmpty() && f.id == story.last() && overset() && context.overset.isValid()) {
            const qreal s = 8 * w;
            painter->fillRect(QRectF(r.right() - s, r.bottom() - s, s, s), context.overset);
        }
    }
}

} // namespace QindaTK
