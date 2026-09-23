// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_layout.h"

namespace QindaTK {

// Measured table: collapsed borders (width `border`), rows including their
// top border, cells with their paragraphs' lines flattened for row splits.
struct RichTextLayout::TableGeometry {
    struct CellLine {
        int paragraph = 0;      // index in the cell
        int line = 0;           // line in that paragraph
        qreal top = 0;          // cell content coordinates
        qreal bottom = 0;
    };
    struct Cell {
        QVector<QUuid> paragraphs;
        QVector<qreal> paragraphTop; // top of each paragraph's first line
        QVector<CellLine> lines;
        qreal contentHeight = 0;
        qreal contentX = 0;          // relative to the table's left edge
        qreal contentWidth = 0;
    };
    quint64 revision = 0;
    qreal width = 0;
    qreal border = 0;
    qreal padding = 0;
    QVector<qreal> columnX;
    QVector<qreal> columnWidth;
    QVector<QVector<Cell>> cells;    // [row][column]
    QVector<qreal> rowHeight;        // top border + padding + content + padding
    qreal headerHeight = 0;

    // Height of row `row` when each cell starts at `firstLine` (a split row).
    qreal remainingRowHeight(int row, const QVector<int> &firstLine) const;
};

namespace RichText {

// Builds pages (or the single continuous flow) from a start state.
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
