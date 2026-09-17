// SPDX-License-Identifier: LGPL-3.0-or-later
#include "grid.h"

#include <QHash>
#include <QtQml/qqml.h>

#include <algorithm>
#include <vector>

namespace QindaTK {

namespace {

struct GridEntry final {
    QQuickItem *item = nullptr;
    GridAttached *attached = nullptr;
    int row = -1;
    int column = -1;
    int rowSpan = 1;
    int columnSpan = 1;
    bool placed = false;
    qreal minW = 0, maxW = LayoutAttachedBase::unbounded;
    qreal minH = 0, maxH = LayoutAttachedBase::unbounded;
    int justifySelf = Grid::Auto;
    int alignSelf = Grid::Auto;
};

struct AreaRect final {
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
};

class Occupancy final {
public:
    [[nodiscard]] bool fits(int row, int column, int rowSpan, int columnSpan) const
    {
        for (int r = row; r < row + rowSpan; ++r) {
            for (int c = column; c < column + columnSpan; ++c) {
                if (taken(r, c)) {
                    return false;
                }
            }
        }
        return true;
    }
    void mark(int row, int column, int rowSpan, int columnSpan)
    {
        for (int r = row; r < row + rowSpan; ++r) {
            for (int c = column; c < column + columnSpan; ++c) {
                m_cells.insert(key(r, c));
            }
        }
    }

private:
    [[nodiscard]] static qint64 key(int r, int c) { return (static_cast<qint64>(r) << 32) | static_cast<quint32>(c); }
    [[nodiscard]] bool taken(int r, int c) const { return m_cells.contains(key(r, c)); }
    QSet<qint64> m_cells;
};

qreal clampTo(qreal value, qreal lo, qreal hi)
{
    return std::max(lo, std::min(value, hi));
}

QHash<QString, AreaRect> parseAreas(const QStringList &areas, int *width)
{
    QHash<QString, AreaRect> rects;
    *width = 0;
    for (int r = 0; r < areas.size(); ++r) {
        const QStringList names = areas[r].split(QLatin1Char(' '), Qt::SkipEmptyParts);
        *width = std::max(*width, static_cast<int>(names.size()));
        for (int c = 0; c < names.size(); ++c) {
            const QString &name = names[c];
            if (name == QLatin1String(".")) {
                continue;
            }
            auto it = rects.find(name);
            if (it == rects.end()) {
                rects.insert(name, {r, c, 1, 1});
            } else {
                AreaRect &rect = it.value();
                rect.rowSpan = std::max(rect.rowSpan, r - rect.row + 1);
                rect.columnSpan = std::max(rect.columnSpan, c - rect.column + 1);
            }
        }
    }
    return rects;
}

GridTrack trackAt(const QVector<GridTrack> &explicitTracks, const QVector<GridTrack> &autoTracks, int index)
{
    if (index < explicitTracks.size()) {
        return explicitTracks[index];
    }
    if (autoTracks.isEmpty()) {
        return {{GridLength::Auto, 0}, {GridLength::Auto, 0}};
    }
    return autoTracks[(index - explicitTracks.size()) % autoTracks.size()];
}

} // namespace

// ---- GridAttached --------------------------------------------------------

GridAttached::GridAttached(QObject *parent)
    : LayoutAttachedBase(parent)
{
}

// ---- Grid ----------------------------------------------------------------

Grid::Grid(QQuickItem *parent)
    : QQuickItem(parent)
{
    m_autoColumnTracks = parseGridTracks(m_autoColumns);
    m_autoRowTracks = parseGridTracks(m_autoRows);
}

Grid::~Grid() = default;

GridAttached *Grid::qmlAttachedProperties(QObject *object)
{
    return new GridAttached(object);
}

void Grid::setColumns(const QString &value)
{
    if (value == m_columns) {
        return;
    }
    m_columns = value;
    QString error;
    m_columnTracks = parseGridTracks(value, &error);
    reportError(error);
    emit layoutPropertyChanged();
    invalidateLayout();
}

void Grid::setRows(const QString &value)
{
    if (value == m_rows) {
        return;
    }
    m_rows = value;
    QString error;
    m_rowTracks = parseGridTracks(value, &error);
    reportError(error);
    emit layoutPropertyChanged();
    invalidateLayout();
}

void Grid::setAutoColumns(const QString &value)
{
    if (value == m_autoColumns) {
        return;
    }
    m_autoColumns = value;
    QString error;
    m_autoColumnTracks = parseGridTracks(value, &error);
    reportError(error);
    emit layoutPropertyChanged();
    invalidateLayout();
}

void Grid::setAutoRows(const QString &value)
{
    if (value == m_autoRows) {
        return;
    }
    m_autoRows = value;
    QString error;
    m_autoRowTracks = parseGridTracks(value, &error);
    reportError(error);
    emit layoutPropertyChanged();
    invalidateLayout();
}

void Grid::reportError(const QString &message)
{
    if (message == m_error) {
        return;
    }
    m_error = message;
    if (!message.isEmpty()) {
        qWarning("QindaTK.Grid: %s", qPrintable(message));
    }
    emit errorChanged();
}

void Grid::invalidateLayout()
{
    if (m_inLayout) {
        m_dirtyDuringLayout = true;
        return;
    }
    polish();
}

void Grid::relayout()
{
    doLayout();
}

void Grid::componentComplete()
{
    QQuickItem::componentComplete();
    for (QQuickItem *child : childItems()) {
        watch(child);
    }
    polish();
}

void Grid::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemChildAddedChange) {
        watch(value.item);
        invalidateLayout();
    } else if (change == ItemChildRemovedChange) {
        unwatch(value.item);
        invalidateLayout();
    } else if (change == ItemVisibleHasChanged && value.boolValue) {
        invalidateLayout();
    }
}

void Grid::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateLayout();
    }
}

void Grid::updatePolish()
{
    doLayout();
}

void Grid::watch(QQuickItem *child)
{
    if (child == nullptr) {
        return;
    }
    connect(child, &QQuickItem::implicitWidthChanged, this, &Grid::invalidateLayout, Qt::UniqueConnection);
    connect(child, &QQuickItem::implicitHeightChanged, this, &Grid::invalidateLayout, Qt::UniqueConnection);
    connect(child, &QQuickItem::visibleChanged, this, &Grid::invalidateLayout, Qt::UniqueConnection);
}

void Grid::unwatch(QQuickItem *child)
{
    if (child != nullptr) {
        disconnect(child, nullptr, this, nullptr);
    }
}

void Grid::doLayout()
{
    if (!isComponentComplete() || m_inLayout) {
        return;
    }
    m_inLayout = true;
    const qreal padL = resolvedPadding(m_paddingLeft);
    const qreal padT = resolvedPadding(m_paddingTop);
    const qreal padR = resolvedPadding(m_paddingRight);
    const qreal padB = resolvedPadding(m_paddingBottom);
    const qreal colGap = m_columnGap >= 0 ? m_columnGap : m_gap;
    const qreal rowGap = m_rowGap >= 0 ? m_rowGap : m_gap;
    const qreal innerW = width() > 0 ? width() - padL - padR : 0;
    const qreal innerH = height() > 0 ? height() - padT - padB : 0;

    int areaWidth = 0;
    const QHash<QString, AreaRect> areas = parseAreas(m_areas, &areaWidth);

    std::vector<GridEntry> entries;
    for (QQuickItem *child : childItems()) {
        auto *attached = qobject_cast<GridAttached *>(qmlAttachedPropertiesObject<Grid>(child, false));
        if (!child->isVisible() || child->inherits("QQuickRepeater") || (attached && attached->ignore())) {
            continue;
        }
        GridEntry e;
        e.item = child;
        e.attached = attached;
        if (attached) {
            e.row = attached->row();
            e.column = attached->column();
            e.rowSpan = attached->rowSpan();
            e.columnSpan = attached->columnSpan();
            e.minW = attached->minWidth();
            e.maxW = attached->maxWidth();
            e.minH = attached->minHeight();
            e.maxH = attached->maxHeight();
            e.justifySelf = attached->justifySelf();
            e.alignSelf = attached->alignSelf();
            const auto area = areas.constFind(attached->area());
            if (area != areas.cend()) {
                e.row = area->row;
                e.column = area->column;
                e.rowSpan = area->rowSpan;
                e.columnSpan = area->columnSpan;
            }
        }
        entries.push_back(e);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const GridEntry &a, const GridEntry &b) {
        return (a.attached ? a.attached->order() : 0) < (b.attached ? b.attached->order() : 0);
    });

    // ---- placement (CSS Grid §8.5) ----
    const bool rowFlow = m_autoFlow == Row || m_autoFlow == RowDense;
    const bool dense = m_autoFlow == RowDense || m_autoFlow == ColumnDense;
    int columns = std::max({static_cast<int>(m_columnTracks.size()), areaWidth, 1});
    int rows = std::max({static_cast<int>(m_rowTracks.size()), static_cast<int>(m_areas.size()), 1});
    Occupancy occupied;
    for (GridEntry &e : entries) {
        if (rowFlow) {
            columns = std::max(columns, e.columnSpan);
            if (e.column >= 0) {
                columns = std::max(columns, e.column + e.columnSpan);
            }
        } else {
            rows = std::max(rows, e.rowSpan);
            if (e.row >= 0) {
                rows = std::max(rows, e.row + e.rowSpan);
            }
        }
    }
    for (GridEntry &e : entries) {
        if (e.row >= 0 && e.column >= 0) {
            occupied.mark(e.row, e.column, e.rowSpan, e.columnSpan);
            e.placed = true;
            rows = std::max(rows, e.row + e.rowSpan);
            columns = std::max(columns, e.column + e.columnSpan);
        }
    }
    // Items locked to one axis take the first free slot along the other.
    for (GridEntry &e : entries) {
        if (e.placed) {
            continue;
        }
        if (rowFlow && e.row >= 0) {
            for (int c = 0;; ++c) {
                if (c + e.columnSpan > columns) {
                    columns = c + e.columnSpan;
                }
                if (occupied.fits(e.row, c, e.rowSpan, e.columnSpan)) {
                    e.column = c;
                    break;
                }
            }
        } else if (!rowFlow && e.column >= 0) {
            for (int r = 0;; ++r) {
                if (r + e.rowSpan > rows) {
                    rows = r + e.rowSpan;
                }
                if (occupied.fits(r, e.column, e.rowSpan, e.columnSpan)) {
                    e.row = r;
                    break;
                }
            }
        } else {
            continue;
        }
        occupied.mark(e.row, e.column, e.rowSpan, e.columnSpan);
        e.placed = true;
        rows = std::max(rows, e.row + e.rowSpan);
        columns = std::max(columns, e.column + e.columnSpan);
    }
    int cursorRow = 0;
    int cursorCol = 0;
    for (GridEntry &e : entries) {
        if (e.placed) {
            continue;
        }
        if (dense) {
            cursorRow = 0;
            cursorCol = 0;
        }
        for (;;) {
            if (rowFlow) {
                if (cursorCol + e.columnSpan > columns) {
                    cursorCol = 0;
                    ++cursorRow;
                    continue;
                }
            } else if (cursorRow + e.rowSpan > rows) {
                cursorRow = 0;
                ++cursorCol;
                continue;
            }
            if (occupied.fits(cursorRow, cursorCol, e.rowSpan, e.columnSpan)) {
                break;
            }
            if (rowFlow) {
                ++cursorCol;
            } else {
                ++cursorRow;
            }
        }
        e.row = cursorRow;
        e.column = cursorCol;
        e.placed = true;
        occupied.mark(e.row, e.column, e.rowSpan, e.columnSpan);
        rows = std::max(rows, e.row + e.rowSpan);
        columns = std::max(columns, e.column + e.columnSpan);
    }

    // ---- track lists including implicit tracks ----
    QVector<GridTrack> colTracks;
    QVector<GridTrack> rowTracks;
    for (int c = 0; c < columns; ++c) {
        colTracks.append(trackAt(m_columnTracks, m_autoColumnTracks, c));
    }
    for (int r = 0; r < rows; ++r) {
        rowTracks.append(trackAt(m_rowTracks, m_autoRowTracks, r));
    }

    // ---- columns from implicit widths ----
    QVector<GridContribution> colItems;
    for (const GridEntry &e : entries) {
        colItems.append({e.column, e.columnSpan, clampTo(e.item->implicitWidth(), e.minW, e.maxW)});
    }
    const GridTrackSizes cols = sizeGridTracks(colTracks, colItems, innerW, colGap);
    QVector<qreal> colStart(columns + 1, 0);
    for (int c = 0; c < columns; ++c) {
        colStart[c + 1] = colStart[c] + cols.sizes[c] + colGap;
    }
    for (GridEntry &e : entries) {
        const qreal cellW = colStart[e.column + e.columnSpan] - colStart[e.column] - colGap;
        const Alignment justify = e.justifySelf == Auto ? m_justifyItems : static_cast<Alignment>(e.justifySelf);
        const qreal w = justify == Stretch ? clampTo(cellW, e.minW, e.maxW)
                                           : clampTo(e.item->implicitWidth(), e.minW, e.maxW);
        e.item->setWidth(w);
    }

    // ---- rows from the resulting implicit heights ----
    QVector<GridContribution> rowItems;
    for (const GridEntry &e : entries) {
        rowItems.append({e.row, e.rowSpan, clampTo(e.item->implicitHeight(), e.minH, e.maxH)});
    }
    const GridTrackSizes rws = sizeGridTracks(rowTracks, rowItems, innerH, rowGap);
    QVector<qreal> rowStart(rows + 1, 0);
    for (int r = 0; r < rows; ++r) {
        rowStart[r + 1] = rowStart[r] + rws.sizes[r] + rowGap;
    }

    // ---- position ----
    for (GridEntry &e : entries) {
        const qreal cellX = colStart[e.column];
        const qreal cellY = rowStart[e.row];
        const qreal cellW = colStart[e.column + e.columnSpan] - cellX - colGap;
        const qreal cellH = rowStart[e.row + e.rowSpan] - cellY - rowGap;
        const Alignment justify = e.justifySelf == Auto ? m_justifyItems : static_cast<Alignment>(e.justifySelf);
        const Alignment align = e.alignSelf == Auto ? m_alignItems : static_cast<Alignment>(e.alignSelf);
        const qreal w = e.item->width();
        const qreal h = align == Stretch ? clampTo(cellH, e.minH, e.maxH)
                                         : clampTo(e.item->implicitHeight(), e.minH, e.maxH);
        qreal x = cellX;
        qreal y = cellY;
        if (justify == Center) x += (cellW - w) / 2;
        else if (justify == End) x += cellW - w;
        if (align == Center) y += (cellH - h) / 2;
        else if (align == End) y += cellH - h;
        e.item->setPosition(QPointF(padL + x, padT + y));
        e.item->setSize(QSizeF(w, h));
    }

    qreal naturalW = colGap * std::max(columns - 1, 0);
    for (qreal v : cols.natural) naturalW += v;
    qreal naturalH = rowGap * std::max(rows - 1, 0);
    for (qreal v : rws.natural) naturalH += v;
    setImplicitSize(naturalW + padL + padR, naturalH + padT + padB);
    const qreal contentW = std::max(naturalW, colStart[columns] - colGap) + padL + padR;
    const qreal contentH = std::max(naturalH, rowStart[rows] - rowGap) + padT + padB;
    if (contentW != m_contentWidth || contentH != m_contentHeight || columns != m_columnCount || rows != m_rowCount) {
        m_contentWidth = contentW;
        m_contentHeight = contentH;
        m_columnCount = columns;
        m_rowCount = rows;
        emit contentSizeChanged();
    }
    m_inLayout = false;
    if (m_dirtyDuringLayout) {
        m_dirtyDuringLayout = false;
        polish();
    }
}

} // namespace QindaTK
