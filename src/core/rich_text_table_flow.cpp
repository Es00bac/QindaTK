// SPDX-License-Identifier: LGPL-3.0-or-later
// Table measurement (merged cells, table styles) and table placement in the page flow.
#include "rich_text_layout_p.h"

namespace QindaTK {
using namespace RichText;

namespace {

qreal total(const QVector<qreal> &values)
{
    qreal sum = 0;
    for (qreal value : values) sum += value;
    return sum;
}

} // namespace

int RichTextLayout::TableGeometry::Cell::fitEnd(int from, qreal height) const
{
    const qreal start = origin(from);
    int end = from;
    while (end < lines.size() && lines.at(end).bottom - start <= height + 0.01) ++end;
    return end;
}

QVector<qreal> RichTextLayout::TableGeometry::segmentHeights(int first, int last, const QVector<int> &resume) const
{
    // A row that continues from the previous page (one of its own cells
    // resumes past line 0) does not repeat its minimum height.
    bool continued = false;
    for (int c = 0; c < columns && !resume.isEmpty(); ++c) {
        const Cell &cell = at(first, c);
        continued = continued || (cell.column == c && cell.row == first && resume.value(c, 0) > 0);
    }
    QVector<qreal> heights;
    heights.reserve(last - first + 1);
    for (int k = first; k <= last; ++k) {
        heights.append(k == first && continued ? 0 : minHeight.at(k));
        // A cell ending at row k needs its remaining lines across its rows here;
        // what its earlier rows lack goes to its last row.
        for (int index : endingAt.at(k)) {
            const Cell &cell = cells.at(index);
            const int start = cell.row <= first ? resume.value(cell.column, 0) : 0;
            const qreal need = border + 2 * padding + cell.contentHeight - cell.origin(start);
            qreal have = 0;
            for (int r = qMax(cell.row, first); r <= k; ++r) have += heights.at(r - first);
            if (need > have) heights[k - first] += need - have;
        }
    }
    return heights;
}

qreal RichTextLayout::TableGeometry::boxContent(const QVector<qreal> &heights, int first, int top, int last) const
{
    qreal height = 0;
    for (int r = top; r <= last; ++r) height += heights.value(r - first);
    return height - border - 2 * padding;
}

QVector<int> RichTextLayout::TableGeometry::continuation(int first, const QVector<qreal> &heights,
                                                          const QVector<int> &resume, int breakRow) const
{
    QVector<int> lines(columns, 0);
    bool any = false;
    for (int c = 0; c < columns; ++c) {
        const Cell &cell = at(breakRow, c);
        if (cell.column != c || cell.row >= breakRow) continue;
        const int from = cell.row <= first ? resume.value(c, 0) : 0;
        lines[c] = cell.fitEnd(from, boxContent(heights, first, qMax(cell.row, first), breakRow - 1));
        any = any || lines.at(c) > 0;
    }
    return any ? lines : QVector<int>{};
}

QVector<RichTextLayout::TableGeometry::VisibleCell>
RichTextLayout::TableGeometry::visibleCells(const PageItem &item) const
{
    QVector<VisibleCell> result;
    const int last = qMin(item.first + item.count, rows) - 1;
    qreal rowTop = 0;
    for (int r = item.first; r <= last; ++r) {
        for (int c = 0; c < columns; ++c) {
            const Cell &cell = at(r, c);
            if (cell.column != c || qMax(cell.row, item.first) != r) continue;
            const int bottom = qMin(cell.lastRow(), last);
            qreal height = 0;
            for (int k = r; k <= bottom; ++k) height += item.rowHeights.value(k - item.first);
            const int start = cell.row <= item.first ? item.cellFirstLine.value(c, 0) : 0;
            const int end = cell.fitEnd(start, boxContent(item.rowHeights, item.first, r, bottom));
            result.append({&cell, rowTop, height, start, end});
        }
        rowTop += item.rowHeights.value(r - item.first);
    }
    return result;
}

std::shared_ptr<const RichTextLayout::TableGeometry> RichTextLayout::tableGeometry(const Table &table, qreal width)
{
    // Up to four widths per table (a table continued across frames of other
    // widths); a stale revision is rebuilt in place. Callers hold the shared
    // pointer, so a replaced entry stays valid for them.
    QVector<std::shared_ptr<TableGeometry>> &entries = m_tables[table.id];
    int reuse = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (!qFuzzyCompare(entries.at(i)->width + 1, width + 1)) continue;
        if (entries.at(i)->revision == table.revision) return entries.at(i);
        reuse = i;
    }
    if (reuse < 0) {
        if (entries.size() >= 4) entries.removeFirst();
        entries.append(std::shared_ptr<TableGeometry>());
        reuse = int(entries.size()) - 1;
    }
    std::shared_ptr<TableGeometry> &slot = entries[reuse];
    auto g = std::make_shared<TableGeometry>();
    g->revision = table.revision;
    g->width = width;
    const TableStyle style = m_document->tableStyle(table);
    g->border = style.borderPt.value_or(table.borderPt) * PxPerPt;
    g->padding = style.paddingPt.value_or(table.cellPaddingPt) * PxPerPt;
    g->rows = int(table.rows.size());
    g->columns = int(table.columnWeights.size());
    double weights = 0;
    for (double weight : table.columnWeights) weights += weight;
    g->inner = qMax<qreal>(1, width - g->border);
    qreal x = 0;
    for (double weight : table.columnWeights) {
        const qreal w = g->inner * weight / weights;
        g->columnX.append(x);
        g->columnWidth.append(w);
        x += w;
    }
    TableGridMap map;
    mapTableGrid(table, &map);
    g->cellAt.resize(g->rows * g->columns);
    g->endingAt.resize(g->rows);
    for (int r = 0; r < g->rows; ++r) {
        const TableRow &row = table.rows.at(r);
        g->minHeight.append(row.minHeightPt * PxPerPt);
        for (int c = 0; c < g->columns; ++c) {
            const int slotIndex = r * g->columns + c;
            if (map.anchorOf(r, c) != slotIndex) { // covered: part of an earlier cell
                g->cellAt[slotIndex] = g->cellAt.at(map.anchorOf(r, c));
                continue;
            }
            const TableCell &source = row.cells.at(c);
            TableGeometry::Cell cell;
            cell.row = r;
            cell.column = c;
            cell.rowSpan = qBound(1, source.rowSpan, g->rows - r);
            cell.columnSpan = qBound(1, source.columnSpan, g->columns - c);
            cell.contentX = g->columnX.at(c) + g->border + g->padding;
            cell.contentWidth = qMax<qreal>(1, g->columnRight(c + cell.columnSpan) - g->columnX.at(c)
                                                   - g->border - 2 * g->padding);
            // Direct cell colour first, then the table style's header or band colour.
            cell.background = source.background.isValid() ? source.background
                : r < table.headerRows ? style.headerBackground.value_or(QColor())
                : (r - table.headerRows) % 2 == 1 ? style.bandBackground.value_or(QColor()) : QColor();
            qreal y = 0;
            const auto &paragraphs = source.paragraphs;
            for (int i = 0; i < paragraphs.size(); ++i) {
                const ParagraphMetrics &m = ensureParagraph(*paragraphs.at(i), cell.contentWidth);
                if (i > 0) y += m.spaceBefore;
                cell.paragraphs.append(paragraphs.at(i)->id);
                cell.paragraphTop.append(y);
                for (int l = 0; l < m.lines.size(); ++l)
                    cell.lines.append({i, l, y + m.lines.at(l).y, y + m.lines.at(l).y + m.lines.at(l).height});
                y += m.height + (i + 1 < paragraphs.size() ? m.spaceAfter : 0);
            }
            cell.contentHeight = y;
            g->cellAt[slotIndex] = int(g->cells.size());
            g->endingAt[cell.lastRow()].append(int(g->cells.size()));
            g->cells.append(cell);
        }
    }
    if (g->rows > 0) g->rowHeight = g->segmentHeights(0, g->rows - 1, {});
    // Row groups: rows joined by cells spanning them stay on one page when they fit.
    g->groupEnd.resize(g->rows);
    int start = 0;
    int end = -1;
    for (int r = 0; r < g->rows; ++r) {
        if (r > end) {
            for (int k = start; k < r; ++k) g->groupEnd[k] = end;
            start = r;
            end = r;
        }
        for (int c = 0; c < g->columns; ++c) {
            const TableGeometry::Cell &cell = g->at(r, c);
            if (cell.row == r && cell.column == c) end = qMax(end, cell.lastRow());
        }
    }
    for (int k = start; k < g->rows; ++k) g->groupEnd[k] = end;
    for (int r = 0; r < qMin(table.headerRows, g->rows); ++r) g->headerHeight += g->rowHeight.at(r);
    slot = g;
    return slot;
}

namespace RichText {

PageItem FlowBuilder::tableItem(const Table &table, int first, const QVector<qreal> &heights,
                                const QVector<int> &resume, qreal y, bool headerRepeat, qreal border,
                                qreal width) const
{
    PageItem item;
    item.kind = PageItem::TableRows;
    item.block = table.id;
    item.first = first;
    item.count = int(heights.size());
    item.y = y;
    item.width = width;
    item.rowHeights = heights;
    item.cellFirstLine = resume;
    item.headerRepeat = headerRepeat;
    // A repeated header has no bottom border: the rows below draw their top one.
    item.height = total(heights) + (headerRepeat ? 0 : border);
    return item;
}

bool FlowBuilder::placeTable(const Table &table, FlowState &state, Page &page)
{
    // AGENT-NOTE: a cell's continuation lines index its lines at one width,
    // so a table that breaks inside a cell keeps that width in the next
    // region (FlowState::widths); after a break between rows it takes the
    // width of the region it continues in.
    const qreal width = !state.cellLine.isEmpty() && !state.widths.isEmpty() ? state.widths.first().width : m_width;
    const auto geometry = m_layout.tableGeometry(table, width);
    const RichTextLayout::TableGeometry &g = *geometry;
    const int rows = g.rows;
    const qreal b = g.border;
    const qreal limit = m_height + 0.01;
    const int first = state.row;
    const QVector<int> resume = state.cellLine;
    // A table starts where its header rows and first body row group fit together.
    if (m_paged && first == 0 && resume.isEmpty() && m_hasContent && rows > 0) {
        qreal need = b;
        for (int i = 0; i <= g.groupEnd.at(qMin(table.headerRows, rows - 1)); ++i) need += g.rowHeight.at(i);
        if (m_y + need > m_height && need <= m_height) return false;
    }
    const bool repeatHeader = m_paged && table.headerRows > 0 && first >= table.headerRows
        && g.headerHeight + b < m_height / 2;
    if (repeatHeader && !m_hasContent) {
        page.items.append(tableItem(table, 0, g.rowHeight.mid(0, table.headerRows), {}, m_y, true, b, width));
        m_y += g.headerHeight;
    }
    const qreal top = m_y;
    QVector<qreal> heights; // rows [first, first + heights.size()) on this page
    // Ends this region; the table resumes at `row` with its cells at `lines`.
    auto breakAt = [&](int row, const QVector<int> &lines) {
        if (!heights.isEmpty()) {
            page.items.append(tableItem(table, first, heights, resume, top, false, b, width));
            m_hasContent = true;
            m_y = top + total(heights) + b;
        }
        state.row = row;
        state.cellLine = lines;
        state.widths = lines.isEmpty() ? QVector<WidthBreak>{} : QVector<WidthBreak>{{0, width}};
        return false;
    };
    int k = first;
    while (k < rows) {
        const int end = g.groupEnd.at(k);
        const QVector<int> start = k == first ? resume : QVector<int>{};
        const QVector<qreal> group = g.segmentHeights(k, end, start);
        const qreal used = total(heights);
        if (!m_paged || top + used + total(group) + b <= limit) {
            heights += group;
            k = end + 1;
            continue;
        }
        const bool pageHasContent = m_hasContent || !heights.isEmpty();
        const qreal repeat = table.headerRows > 0 && k >= table.headerRows ? g.headerHeight : 0;
        // The row group fits on the next page: keep it together there.
        if (pageHasContent && total(group) + b + repeat <= limit) return breakAt(k, start);
        // A row group taller than a page breaks between its rows; cells that
        // cross the break continue from their first line not shown here.
        QVector<qreal> fitted;
        int count = 0;
        while (k + count < end) {
            const QVector<qreal> trial = g.segmentHeights(k, k + count, start);
            if (top + used + total(trial) + b > limit) break;
            fitted = trial;
            ++count;
        }
        if (count > 0) {
            heights += fitted;
            return breakAt(k + count, g.continuation(first, heights, resume, k + count));
        }
        if (pageHasContent) return breakAt(k, start);
        // Not even row k fits on an empty page: split it at line boundaries.
        return splitRow(table, g, k, top, state, page);
    }
    if (!heights.isEmpty()) page.items.append(tableItem(table, first, heights, resume, top, false, b, width));
    m_y = top + total(heights) + b;
    m_hasContent = true;
    return true;
}

bool FlowBuilder::splitRow(const Table &table, const RichTextLayout::TableGeometry &g, int row, qreal top,
                           FlowState &state, Page &page)
{
    const qreal b = g.border;
    const QVector<int> resume = state.cellLine; // the page is empty: `row` is state.row
    const qreal available = m_height - top - 2 * b - 2 * g.padding;
    QVector<int> ends(g.columns, 0);
    qreal shown = 0;
    for (int c = 0; c < g.columns; ++c) {
        const auto &cell = g.at(row, c);
        if (cell.column != c) continue;
        const int from = resume.value(c, 0);
        int end = cell.fitEnd(from, available);
        if (end == from && from < cell.lines.size()) { // a line taller than the page
            ++end;
            page.overflow = true;
        }
        if (end > from) shown = qMax(shown, cell.lines.at(end - 1).bottom - cell.origin(from));
        ends[c] = end;
    }
    // Every cell shows what fits in the tallest part (visibleCells does the same).
    bool finished = true;
    for (int c = 0; c < g.columns; ++c) {
        const auto &cell = g.at(row, c);
        if (cell.column != c) continue;
        ends[c] = qMax(ends.at(c), cell.fitEnd(resume.value(c, 0), shown));
        if (cell.lastRow() == row) finished = finished && ends.at(c) >= cell.lines.size();
    }
    const qreal width = g.width;
    page.items.append(tableItem(table, row, {b + 2 * g.padding + shown}, resume, top, false, b, width));
    m_hasContent = true;
    m_y = top + 2 * b + 2 * g.padding + shown;
    if (!finished) {
        state.row = row;
        state.cellLine = ends;
        state.widths = {{0, width}};
        return false;
    }
    if (row + 1 >= g.rows) {
        state.widths.clear();
        return true;
    }
    // The row is complete; cells spanning below it continue in the next row.
    QVector<int> next(g.columns, 0);
    bool any = false;
    for (int c = 0; c < g.columns; ++c) {
        const auto &cell = g.at(row + 1, c);
        if (cell.column != c || cell.row > row) continue;
        next[c] = ends.at(c);
        any = any || next.at(c) > 0;
    }
    state.row = row + 1;
    state.cellLine = any ? next : QVector<int>{};
    state.widths = any ? QVector<WidthBreak>{{0, width}} : QVector<WidthBreak>{};
    return false;
}

} // namespace RichText
} // namespace QindaTK
