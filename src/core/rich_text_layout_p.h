// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_layout.h"

namespace QindaTK {

// Measured table at one width: collapsed borders (width `border`), rows
// including their top border, and one entry per visible cell (a plain cell
// or the anchor of a merged cell) with its paragraphs' lines flattened.
// AGENT-CONTRACT (docs/publishing.md#merged-cells): FlowBuilder::placeTable,
// pageLines, pageRange and paintTable agree on which lines a cell shows
// because all of them use visibleCells()/boxContent()/fitEnd().
struct RichTextLayout::TableGeometry {
    struct CellLine {
        int paragraph = 0;      // index in the cell
        int line = 0;           // line in that paragraph
        qreal top = 0;          // cell content coordinates
        qreal bottom = 0;
    };
    struct Cell {
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
        QVector<QUuid> paragraphs;
        QVector<qreal> paragraphTop; // top of each paragraph's first line
        QVector<CellLine> lines;
        qreal contentHeight = 0;
        qreal contentX = 0;          // relative to the table's left edge
        qreal contentWidth = 0;
        QColor background;           // the cell's own, else its table style's
        int lastRow() const { return row + rowSpan - 1; }
        // Content offset of line `line` (the content height past the last line).
        qreal origin(int line) const { return line < lines.size() ? lines.at(line).top : contentHeight; }
        // End of the lines from `from` that fit in `height` of content.
        int fitEnd(int from, qreal height) const;
    };
    // A cell as one TableRows item shows it: box offset and height within
    // the item (px), and the lines [start, end) inside the box.
    struct VisibleCell {
        const Cell *cell = nullptr;
        qreal top = 0;
        qreal height = 0;
        int start = 0;
        int end = 0;
    };
    quint64 revision = 0;
    qreal width = 0;
    qreal inner = 0;                 // width minus the right border
    qreal border = 0;
    qreal padding = 0;
    int rows = 0;
    int columns = 0;
    QVector<qreal> columnX;
    QVector<qreal> columnWidth;
    QVector<Cell> cells;             // visible cells, row-major by anchor
    QVector<int> cellAt;              // [row * columns + column] -> index in `cells`
    QVector<QVector<int>> endingAt;  // [row] cells whose last row it is
    QVector<qreal> minHeight;        // per row
    QVector<qreal> rowHeight;        // whole table on one page
    QVector<int> groupEnd;           // [row] last row of its row group (rows joined by merged cells)
    qreal headerHeight = 0;

    const Cell &at(int row, int column) const { return cells.at(cellAt.at(row * columns + column)); }
    qreal columnRight(int column) const { return column < columns ? columnX.at(column) : inner; }
    // Heights of rows [first, last] placed together when the cells covering
    // `first` resume at `resume` (by first column; empty: from line 0).
    // Cells ending inside the range get room for their lines; cells running
    // past `last` are clipped there and constrain nothing.
    QVector<qreal> segmentHeights(int first, int last, const QVector<int> &resume) const;
    // Content height of a cell box over rows [top, last] of a segment that starts at `first`.
    qreal boxContent(const QVector<qreal> &heights, int first, int top, int last) const;
    // Where the cells running past a break before `breakRow` resume, after
    // rows [first, breakRow) with `heights` (empty: all from line 0).
    QVector<int> continuation(int first, const QVector<qreal> &heights, const QVector<int> &resume,
                              int breakRow) const;
    QVector<VisibleCell> visibleCells(const RichText::PageItem &item) const;
};

namespace RichText {

// Builds pages (or the single continuous flow) from a start state. One
// builder fills one region: a page content box, a page column or a frame column.
class FlowBuilder {
public:
    FlowBuilder(RichTextLayout &layout, const RichTextDocument &document, qreal width,
                qreal height, bool paged);
    // Fills `page` from `start`; returns false at the end of the document.
    bool build(const FlowState &start, Page *page, FlowState *next);

private:
    struct FloatBox { QRectF rect; QString objectId; qreal gap = 0; bool left = true; };
    bool placeParagraph(const Paragraph &p, FlowState &state, Page &page);
    bool placeTable(const Table &table, FlowState &state, Page &page);
    bool splitRow(const Table &table, const RichTextLayout::TableGeometry &g, int row, qreal top,
                  FlowState &state, Page &page);
    PageItem tableItem(const Table &table, int first, const QVector<qreal> &heights,
                       const QVector<int> &resume, qreal y, bool headerRepeat, qreal border,
                       qreal width) const;
    QVector<Exclusion> exclusionsAt(qreal top, const QVector<FloatBox> &own) const;
    qreal firstLineHeight(int block);
    QVector<FloatBox> floatsOf(const Paragraph &p, qreal top, bool rightToLeft) const;
    void addObjects(const Paragraph &p, const QVector<FloatBox> &floats, Page &page);

    RichTextLayout &m_layout;
    const RichTextDocument &m_document;
    qreal m_width;
    qreal m_height;
    bool m_paged;
    qreal m_y = 0;
    bool m_hasContent = false;
    QVector<FloatBox> m_floats;
};

// Flow states match when they resume the same content in the same context;
// block indexes may differ after insertions earlier in the document.
bool sameFlow(const FlowState &a, const FlowState &b);

} // namespace RichText
} // namespace QindaTK
