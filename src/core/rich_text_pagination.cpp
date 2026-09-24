// SPDX-License-Identifier: LGPL-3.0-or-later
// Page flow and incremental pagination of RichTextLayout (tables: rich_text_table_flow.cpp).
#include "rich_text_layout_p.h"
#include "rich_text_shaping_p.h"
#include "rich_text_spans.h"

#include <QElapsedTimer>
#include <QMultiHash>
#include <limits>

namespace QindaTK {
using namespace RichText;

namespace RichText {

bool sameFlow(const FlowState &a, const FlowState &b)
{
    return a.blockId == b.blockId && a.line == b.line && a.row == b.row
        && a.cellLine == b.cellLine && a.exclusions == b.exclusions;
}

FlowBuilder::FlowBuilder(RichTextLayout &layout, const RichTextDocument &document, qreal width,
                         qreal height, bool paged)
    : m_layout(layout), m_document(document), m_width(width), m_height(height), m_paged(paged)
{
}

bool FlowBuilder::build(const FlowState &start, Page *page, FlowState *next)
{
    *page = Page{};
    page->start = start;
    m_y = 0;
    m_hasContent = false;
    m_floats.clear();
    FlowState state = start;
    const auto &blocks = m_document.blocks();
    while (state.block < blocks.size()) {
        const Block &block = blocks.at(state.block);
        state.blockId = block.id();
        const bool placed = block.paragraph ? placeParagraph(*block.paragraph, state, *page)
                                            : placeTable(*block.table, state, *page);
        if (!placed) {
            *next = state;
            return true;
        }
        ++state.block;
        state.line = state.row = 0;
        state.cellLine.clear();
        state.exclusions.clear();
        if (state.block < blocks.size()) state.blockId = blocks.at(state.block).id();
    }
    *next = state;
    return false;
}

QVector<FlowBuilder::FloatBox> FlowBuilder::floatsOf(const Paragraph &p, qreal top, bool rightToLeft) const
{
    QVector<FloatBox> floats;
    for (int i = 0; i < p.text.size(); ++i) {
        if (p.text.at(i) != QChar(ObjectChar)) continue;
        const RichObject *o = m_document.object(m_document.charFormat(Spans::formatAt(p.spans, i)).objectId);
        if (!o || (o->placement != Placement::FloatStart && o->placement != Placement::FloatEnd)) continue;
        const qreal w = qMin(o->widthPt * PxPerPt, m_width);
        const bool left = (o->placement == Placement::FloatStart) != rightToLeft;
        floats.append({QRectF(left ? 0 : m_width - w, top + o->offsetYPt * PxPerPt, w, o->heightPt * PxPerPt),
                       o->id, o->gapPt * PxPerPt, left});
    }
    return floats;
}

QVector<Exclusion> FlowBuilder::exclusionsAt(qreal top, const QVector<FloatBox> &own) const
{
    QVector<Exclusion> result;
    auto add = [&](const FloatBox &f) {
        if (f.rect.bottom() + f.gap <= top) return;
        result.append({f.rect.top() - top, f.rect.bottom() + f.gap - top,
                       f.left ? 0 : f.rect.left() - f.gap, f.left ? f.rect.right() + f.gap : m_width});
    };
    for (const FloatBox &f : m_floats) add(f);
    for (const FloatBox &f : own) add(f);
    return result;
}

qreal FlowBuilder::firstLineHeight(int block)
{
    const auto &blocks = m_document.blocks();
    if (block >= blocks.size()) return 0;
    if (const Paragraph *p = blocks.at(block).paragraph.get()) {
        const ParagraphMetrics &m = m_layout.ensureParagraph(*p, m_width);
        return m.spaceBefore + (m.lines.isEmpty() ? 0 : m.lines.first().height);
    }
    const auto &geometry = m_layout.tableGeometry(*blocks.at(block).table);
    const int rows = qMin(int(geometry.rowHeight.size()), blocks.at(block).table->headerRows + 1);
    qreal height = geometry.border;
    for (int r = 0; r < rows; ++r) height += geometry.rowHeight.at(r);
    return height;
}

void FlowBuilder::addObjects(const Paragraph &p, const QVector<FloatBox> &floats, Page &page)
{
    for (const FloatBox &f : floats) {
        PageItem item;
        item.kind = PageItem::Float;
        item.block = p.id;
        item.objectId = f.objectId;
        item.x = f.rect.x();
        item.y = f.rect.y();
        item.width = f.rect.width();
        item.height = f.rect.height();
        page.items.append(item);
        page.overflow = page.overflow || (m_paged && f.rect.bottom() > m_height + 0.01);
        m_floats.append(f);
    }
    for (int i = 0; i < p.text.size(); ++i) {
        if (p.text.at(i) != QChar(ObjectChar)) continue;
        const RichObject *o = m_document.object(m_document.charFormat(Spans::formatAt(p.spans, i)).objectId);
        if (!o || o->placement != Placement::Page) continue;
        PageItem item;
        item.kind = PageItem::PageObject;
        item.block = p.id;
        item.objectId = o->id;
        item.x = o->offsetXPt * PxPerPt;
        item.y = o->offsetYPt * PxPerPt;
        item.width = o->widthPt * PxPerPt;
        item.height = o->heightPt * PxPerPt;
        page.items.append(item);
    }
}

bool FlowBuilder::placeParagraph(const Paragraph &p, FlowState &state, Page &page)
{
    const bool continuation = state.line > 0;
    if (m_paged && !continuation && p.format.pageBreakBefore && state.block > 0 && m_hasContent)
        return false;
    const qreal top = m_y + (m_hasContent && !continuation ? p.format.spaceBeforePt * PxPerPt : 0);
    const bool rightToLeft = Shaping::paragraphIsRightToLeft(p);
    const QVector<FloatBox> own = continuation ? QVector<FloatBox>{} : floatsOf(p, top, rightToLeft);
    // AGENT-GUARD: a float stays on the page of its anchor paragraph's first
    // line; if it would cross the page bottom, the paragraph moves with it.
    if (m_paged && m_hasContent)
        for (const FloatBox &f : own)
            if (f.rect.bottom() > m_height + 0.01) return false;
    const QVector<Exclusion> exclusions = continuation ? state.exclusions : exclusionsAt(top, own);
    const bool keepWithNext = p.format.keepWithNext || p.format.headingLevel > 0;
    const qreal next = m_paged && !continuation && m_hasContent && keepWithNext
        ? firstLineHeight(state.block + 1) : 0;
    ParagraphMetrics &m = m_layout.ensureParagraph(p, m_width, exclusions);
    const int n = int(m.lines.size());
    const int first = qMin(state.line, n - 1);
    const qreal linesTop = m.lines.at(first).y;
    auto bottomOf = [&](int line) { return top + m.lines.at(line).y + m.lines.at(line).height - linesTop; };
    if (m_paged && !continuation && m_hasContent) {
        if (p.format.keepTogether && m.height <= m_height && top + m.height > m_height) return false;
        if (m.keepWithNext && next > 0 && m.height + next <= m_height
            && top + m.height + m.spaceAfter + next > m_height)
            return false;
    }
    int fit = 0;
    while (first + fit < n && (!m_paged || bottomOf(first + fit) <= m_height + 0.01)) ++fit;
    const bool split = first + fit < n;
    if (split) {
        if (n - (first + fit) == 1 && fit >= 2) --fit;                 // widow: carry two lines
        if (first == 0 && fit == 1 && n > 1 && m_hasContent) fit = 0;  // orphan: move it all
        if (fit == 0 && !m_hasContent) {
            fit = 1;
            page.overflow = page.overflow || bottomOf(first) > m_height + 0.01;
        }
    } else {
        fit = n - first;
    }
    if (fit > 0) {
        PageItem item;
        item.kind = PageItem::Lines;
        item.block = p.id;
        item.first = first;
        item.count = fit;
        item.y = top;
        item.width = m_width;
        item.height = bottomOf(first + fit - 1) - top;
        item.exclusions = exclusions;
        page.items.append(item);
        if (first == 0) addObjects(p, own, page);
        m_hasContent = true;
    }
    if (split) {
        state.line = first + fit;
        state.exclusions = fit > 0 ? exclusions : QVector<Exclusion>{};
        return false;
    }
    m_y = bottomOf(n - 1) + m.spaceAfter;
    return true;
}

} // namespace RichText

QSizeF RichTextLayout::pageSize() const
{
    if (m_mode == Mode::Continuous) return QSizeF(m_flowWidth, m_flowHeight);
    const PageSetup page = m_document ? m_document->pageSetupValue() : PageSetup{};
    return QSizeF(page.widthPt * PxPerPt, page.heightPt * PxPerPt);
}

QRectF RichTextLayout::contentRect() const
{
    if (m_mode == Mode::Continuous) return QRectF(0, 0, m_flowWidth, m_flowHeight);
    const PageSetup page = m_document ? m_document->pageSetupValue() : PageSetup{};
    return QRectF(page.marginLeftPt * PxPerPt, page.marginTopPt * PxPerPt,
                  page.contentWidthPt() * PxPerPt, page.contentHeightPt() * PxPerPt);
}

qreal RichTextLayout::flowHeight() const { return m_flowHeight; }

int RichTextLayout::estimatedPageCount() const
{
    if (m_complete || !m_document || m_pages.isEmpty()) return pageCount();
    const int blocks = int(m_document->blocks().size());
    const int done = qMax(1, m_frontier.block);
    return qMax(pageCount(), int(qint64(pageCount()) * blocks / done));
}

QVector<QUuid> RichTextLayout::blockIds() const
{
    QVector<QUuid> ids;
    if (!m_document) return ids;
    ids.reserve(m_document->blocks().size());
    for (const Block &block : m_document->blocks()) ids.append(block.id());
    return ids;
}

void RichTextLayout::resetStats()
{
    m_stats = LayoutStats{};
}

void RichTextLayout::invalidateAll()
{
    m_metrics.clear();
    m_contextMetrics.clear();
    m_tables.clear();
    m_pages.clear();
    m_shapedCount = 0;
    m_flowHeight = 0;
    m_complete = !m_document;
    m_frontier = FlowState{};
    if (m_document && !m_document->blocks().isEmpty()) m_frontier.blockId = m_document->blocks().first().id();
    m_paginatedBlocks.clear();
    rebuildMarkers();
    m_markersChanged = false; // every page is rebuilt anyway
    emit layoutReset();
    emit pagesChanged(0, 0);
}

void RichTextLayout::continuePagination(qint64 budgetNs, int stopPage)
{
    if (!m_document || m_complete) return;
    QElapsedTimer timer;
    timer.start();
    const bool paged = m_mode == Mode::Paged;
    const qreal width = paged ? contentRect().width() : m_flowWidth;
    const qreal height = paged ? contentRect().height() : std::numeric_limits<qreal>::infinity();
    FlowBuilder builder(*this, *m_document, width, height, paged);
    const int firstNew = pageCount();
    while (!m_complete && pageCount() < stopPage) {
        Page page;
        FlowState next;
        const bool more = builder.build(m_frontier, &page, &next);
        m_pages.append(page);
        ++m_stats.pagesPaginated;
        m_frontier = next;
        m_complete = !more;
        if (timer.nsecsElapsed() > budgetNs) break;
    }
    finalizePages(firstNew);
}

void RichTextLayout::ensurePaginated(int pageIndex)
{
    continuePagination(std::numeric_limits<qint64>::max(), pageIndex + 1);
}

bool RichTextLayout::isLaidOut(const Position &position) const
{
    if (!m_document || m_pages.isEmpty()) return false;
    if (m_complete) return true;
    const Paragraph *p = m_document->paragraph(position.paragraph);
    if (!p) return false;
    m_document->paragraphsInOrder();
    return (p->table ? p->table->blockIndex : p->blockIndex) < m_frontier.block;
}

void RichTextLayout::ensurePaginatedThrough(const Position &position)
{
    if (!m_document) return;
    const Paragraph *p = m_document->paragraph(position.paragraph);
    if (!p) return;
    m_document->paragraphsInOrder();
    const int block = p->table ? p->table->blockIndex : p->blockIndex;
    while (!m_complete && m_frontier.block <= block)
        continuePagination(std::numeric_limits<qint64>::max(), pageCount() + 1);
}

void RichTextLayout::paginateAll()
{
    continuePagination(std::numeric_limits<qint64>::max(), std::numeric_limits<int>::max());
}

bool RichTextLayout::paginateStep(qint64 budgetNs)
{
    continuePagination(budgetNs, std::numeric_limits<int>::max());
    return !m_complete;
}

void RichTextLayout::onContentChanged(const ChangeSet &change)
{
    if (!m_document) return;
    if (change.reset || change.document) {
        invalidateAll();
        return;
    }
    for (const QUuid &id : change.removedParagraphs) m_metrics.erase(id);
    QSet<QString> prefixes;
    for (const QUuid &id : change.paragraphs) prefixes.insert(id.toString());
    // A list definition changed in place moves its items' indents without
    // touching their revisions; membership changes touch the paragraph itself.
    QVector<const Paragraph *> relaid;
    if (!change.listDefinitions.isEmpty())
        for (const Paragraph *p : m_document->paragraphsInOrder())
            if (change.listDefinitions.contains(p->format.listId)) {
                relaid.append(p);
                m_metrics.erase(p->id);
                prefixes.insert(p->id.toString());
            }
    for (auto it = m_contextMetrics.begin(); it != m_contextMetrics.end();) {
        const QString id = it->first.left(38);
        if (prefixes.contains(id) || change.removedParagraphs.contains(QUuid(id))) it = m_contextMetrics.erase(it);
        else ++it;
    }
    if (m_contextMetrics.size() > 1024) m_contextMetrics.clear();
    m_markersDirty = m_markersDirty || change.lists || change.structure;
    const QVector<QUuid> ids = blockIds();
    const QVector<QUuid> &old = m_paginatedBlocks;
    int prefix = 0;
    while (prefix < ids.size() && prefix < old.size() && ids.at(prefix) == old.at(prefix)) ++prefix;
    int suffix = 0;
    while (suffix < ids.size() - prefix && suffix < old.size() - prefix
           && ids.at(ids.size() - 1 - suffix) == old.at(old.size() - 1 - suffix))
        ++suffix;
    int dirtyFirst = ids == old ? std::numeric_limits<int>::max() : prefix;
    int dirtyLast = ids == old ? -1 : int(ids.size()) - 1 - suffix;
    m_document->paragraphsInOrder();
    auto mark = [&](int block) { dirtyFirst = qMin(dirtyFirst, block); dirtyLast = qMax(dirtyLast, block); };
    for (const QUuid &id : change.paragraphs)
        if (const Paragraph *p = m_document->paragraph(id)) mark(p->table ? p->table->blockIndex : p->blockIndex);
    for (const QUuid &id : change.tables)
        if (const Table *t = m_document->table(id)) mark(t->blockIndex);
    for (const Paragraph *p : std::as_const(relaid)) mark(p->table ? p->table->blockIndex : p->blockIndex);
    if (dirtyFirst == std::numeric_limits<int>::max()) {
        finalizePages(pageCount());
        return;
    }
    repaginateFrom(dirtyFirst, dirtyLast);
}

void RichTextLayout::repaginateFrom(int dirtyFirst, int dirtyLast)
{
    QElapsedTimer timer;
    timer.start();
    m_stats.lastConvergedPage = -1;
    int restart = 0;
    const int anchor = qMax(0, dirtyFirst - 1);
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages.at(i).start.block > anchor) break;
        restart = i;
    }
    m_stats.lastRestartPage = restart;
    const bool frontierUntouched = !m_complete && (dirtyFirst > m_frontier.block
        || (dirtyFirst == m_frontier.block && m_frontier.line == 0 && m_frontier.row == 0
            && m_frontier.cellLine.isEmpty()));
    if (m_pages.isEmpty() || frontierUntouched) {
        // Nothing paginated depends on the change; progressive pagination
        // will reach it with the new content.
        m_paginatedBlocks = blockIds();
        return;
    }
    const QVector<Page> old = m_pages.mid(restart);
    FlowState state = old.first().start;
    m_pages.resize(restart);
    if (!m_complete) {
        // Progressive pagination resumes from the restart page.
        m_frontier = state;
        continuePagination(std::numeric_limits<qint64>::max(), restart + 2);
        m_stats.lastRepaginationNs = timer.nsecsElapsed();
        return;
    }
    QMultiHash<QUuid, int> oldByBlock;
    for (int i = 1; i < old.size(); ++i) oldByBlock.insert(old.at(i).start.blockId, i);
    const bool paged = m_mode == Mode::Paged;
    const qreal width = paged ? contentRect().width() : m_flowWidth;
    const qreal height = paged ? contentRect().height() : std::numeric_limits<qreal>::infinity();
    FlowBuilder builder(*this, *m_document, width, height, paged);
    for (;;) {
        Page page;
        FlowState next;
        const bool more = builder.build(state, &page, &next);
        m_pages.append(page);
        ++m_stats.pagesPaginated;
        if (!more) {
            m_frontier = next;
            break;
        }
        state = next;
        if (state.block <= dirtyLast) continue;
        int reuse = -1;
        for (auto it = oldByBlock.constFind(state.blockId); it != oldByBlock.constEnd() && it.key() == state.blockId; ++it)
            if (sameFlow(old.at(it.value()).start, state) && (reuse < 0 || it.value() < reuse)) reuse = it.value();
        if (reuse < 0) continue;
        // Converged: every later page starts from identical content and state.
        const int delta = state.block - old.at(reuse).start.block;
        m_stats.lastConvergedPage = pageCount();
        for (int k = reuse; k < old.size(); ++k) {
            Page reused = old.at(k);
            reused.start.block += delta;
            m_pages.append(reused);
            ++m_stats.pagesReused;
        }
        m_frontier.block += delta;
        break;
    }
    m_complete = true;
    finalizePages(restart, m_stats.lastConvergedPage);
    m_stats.lastRepaginationNs = timer.nsecsElapsed();
}

} // namespace QindaTK
