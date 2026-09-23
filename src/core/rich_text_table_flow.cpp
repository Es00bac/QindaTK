// SPDX-License-Identifier: LGPL-3.0-or-later
// Table measurement and table placement in the page flow.
#include "rich_text_layout_p.h"

namespace QindaTK {
using namespace RichText;

qreal RichTextLayout::TableGeometry::remainingRowHeight(int row, const QVector<int> &firstLine) const
{
    qreal content = 0;
    const auto &rowCells = cells.at(row);
    for (int c = 0; c < rowCells.size(); ++c) {
        const Cell &cell = rowCells.at(c);
        const int line = firstLine.value(c, 0);
        const qreal origin = line < cell.lines.size() ? cell.lines.at(line).top : cell.contentHeight;
        content = qMax(content, cell.contentHeight - origin);
    }
    return border + 2 * padding + content;
}

const RichTextLayout::TableGeometry &RichTextLayout::tableGeometry(const Table &table)
{
    const qreal width = m_mode == Mode::Paged ? contentRect().width() : m_flowWidth;
    std::shared_ptr<TableGeometry> &slot = m_tables[table.id];
    if (slot && slot->revision == table.revision && qFuzzyCompare(slot->width + 1, width + 1)) return *slot;
    auto g = std::make_shared<TableGeometry>();
    g->revision = table.revision;
    g->width = width;
    g->border = table.borderPt * PxPerPt;
    g->padding = table.cellPaddingPt * PxPerPt;
    double total = 0;
    for (double weight : table.columnWeights) total += weight;
    const qreal inner = qMax<qreal>(1, width - g->border);
    qreal x = 0;
    for (double weight : table.columnWeights) {
        const qreal w = inner * weight / total;
        g->columnX.append(x);
        g->columnWidth.append(w);
        x += w;
    }
    for (int r = 0; r < table.rows.size(); ++r) {
        const TableRow &row = table.rows.at(r);
        QVector<TableGeometry::Cell> cells;
        qreal content = 0;
        for (int c = 0; c < row.cells.size(); ++c) {
            TableGeometry::Cell cell;
            cell.contentX = g->columnX.at(c) + g->border + g->padding;
            cell.contentWidth = qMax<qreal>(1, g->columnWidth.at(c) - g->border - 2 * g->padding);
            qreal y = 0;
            const auto &paragraphs = row.cells.at(c).paragraphs;
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
            content = qMax(content, y);
            cells.append(cell);
        }
        g->cells.append(cells);
        g->rowHeight.append(qMax(g->border + 2 * g->padding + content, row.minHeightPt * PxPerPt));
    }
    for (int r = 0; r < qMin(table.headerRows, int(g->rowHeight.size())); ++r) g->headerHeight += g->rowHeight.at(r);
    slot = g;
    return *slot;
}

namespace RichText {

bool FlowBuilder::placeTable(const Table &table, FlowState &state, Page &page)
{
    const auto &g = m_layout.tableGeometry(table);
    const int rows = int(table.rows.size());
    const qreal b = g.border;
    int r = state.row;
    QVector<int> cellLine = state.cellLine;
    // A table starts where its header rows and first body row fit together.
    if (m_paged && r == 0 && cellLine.isEmpty() && m_hasContent) {
        qreal need = b;
        for (int i = 0; i <= qMin(table.headerRows, rows - 1); ++i) need += g.rowHeight.at(i);
        if (m_y + need > m_height && need <= m_height) return false;
    }
    const bool repeatHeader = m_paged && table.headerRows > 0 && r >= table.headerRows
        && g.headerHeight + b < m_height / 2;
    if (repeatHeader && !m_hasContent) {
        PageItem header;
        header.kind = PageItem::TableRows;
        header.block = table.id;
        header.first = 0;
        header.count = table.headerRows;
        header.y = m_y;
        header.width = m_width;
        header.height = g.headerHeight;
        header.headerRepeat = true;
        page.items.append(header);
        m_y += g.headerHeight;
    }
    PageItem run;
    auto startRun = [&](int row, qreal y) {
        run = PageItem{};
        run.kind = PageItem::TableRows;
        run.block = table.id;
        run.first = row;
        run.y = y;
        run.width = m_width;
    };
    qreal y = m_y;
    startRun(r, y);
    auto flush = [&]() {
        if (run.count == 0) return;
        run.height = y + b - run.y;
        page.items.append(run);
        m_hasContent = true;
    };
    while (r < rows) {
        const bool continuing = !cellLine.isEmpty();
        const qreal h = continuing ? g.remainingRowHeight(r, cellLine) : g.rowHeight.at(r);
        if (!m_paged || y + h + b <= m_height + 0.01) {
            if (!continuing) {
                ++run.count;
                y += h;
                ++r;
                continue;
            }
            flush();
            PageItem part;
            part.kind = PageItem::TableRows;
            part.block = table.id;
            part.first = r;
            part.count = 1;
            part.y = y;
            part.width = m_width;
            part.height = h + b;
            part.cellFirstLine = cellLine;
            page.items.append(part);
            m_hasContent = true;
            y += h;
            ++r;
            cellLine.clear();
            startRun(r, y);
            continue;
        }
        const bool pageHasContent = m_hasContent || run.count > 0;
        const qreal repeat = repeatHeader || (table.headerRows > 0 && r >= table.headerRows) ? g.headerHeight : 0;
        if (pageHasContent && h + b + repeat <= m_height + 0.01) {
            flush();
            m_y = y + (run.count > 0 ? b : 0);
            state.row = r;
            state.cellLine = cellLine;
            return false;
        }
        // Taller than a page: split the row at line boundaries of each cell.
        flush();
        const int columns = int(g.cells.at(r).size());
        const QVector<int> startLines = continuing ? cellLine : QVector<int>(columns, 0);
        const qreal available = m_height - y - 2 * b - 2 * g.padding;
        QVector<int> nextLines(columns);
        qreal shown = 0;
        bool progress = false;
        bool finished = true;
        for (int c = 0; c < columns; ++c) {
            const auto &cell = g.cells.at(r).at(c);
            const int line = startLines.at(c);
            const qreal origin = line < cell.lines.size() ? cell.lines.at(line).top : cell.contentHeight;
            int end = line;
            while (end < cell.lines.size() && cell.lines.at(end).bottom - origin <= available) ++end;
            if (end == line && !pageHasContent && line < cell.lines.size()) {
                ++end;
                page.overflow = true;
            }
            progress = progress || end > line;
            if (end > line) shown = qMax(shown, cell.lines.at(end - 1).bottom - origin);
            nextLines[c] = end;
            finished = finished && end >= cell.lines.size();
        }
        if (!progress) {
            m_y = y;
            state.row = r;
            state.cellLine = cellLine;
            return false;
        }
        PageItem part;
        part.kind = PageItem::TableRows;
        part.block = table.id;
        part.first = r;
        part.count = 1;
        part.y = y;
        part.width = m_width;
        part.height = 2 * b + 2 * g.padding + shown;
        part.cellFirstLine = startLines;
        part.rowClip = qMax<qreal>(shown, 0.001);
        page.items.append(part);
        m_hasContent = true;
        if (!finished) {
            state.row = r;
            state.cellLine = nextLines;
            return false;
        }
        y += part.height - b;
        ++r;
        cellLine.clear();
        startRun(r, y);
    }
    flush();
    m_y = y + b;
    m_hasContent = true;
    return true;
}

} // namespace RichText
} // namespace QindaTK
