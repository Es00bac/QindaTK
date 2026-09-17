// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "grid_tracks.h"
#include "layout_attached.h"

#include <QQuickItem>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class GridAttached : public LayoutAttachedBase {
    Q_OBJECT
    QML_ANONYMOUS
    // 0-based track indices; -1 auto-places.
    Q_PROPERTY(int row READ row WRITE setRow NOTIFY changed)
    Q_PROPERTY(int column READ column WRITE setColumn NOTIFY changed)
    Q_PROPERTY(int rowSpan READ rowSpan WRITE setRowSpan NOTIFY changed)
    Q_PROPERTY(int columnSpan READ columnSpan WRITE setColumnSpan NOTIFY changed)
    // A name from Grid.areas; overrides row/column/spans.
    Q_PROPERTY(QString area READ area WRITE setArea NOTIFY changed)
    Q_PROPERTY(int justifySelf READ justifySelf WRITE setJustifySelf NOTIFY changed)

public:
    explicit GridAttached(QObject *parent = nullptr);

    [[nodiscard]] int row() const { return m_row; }
    void setRow(int value) { assign(m_row, value); }
    [[nodiscard]] int column() const { return m_column; }
    void setColumn(int value) { assign(m_column, value); }
    [[nodiscard]] int rowSpan() const { return m_rowSpan; }
    void setRowSpan(int value) { assign(m_rowSpan, std::max(1, value)); }
    [[nodiscard]] int columnSpan() const { return m_columnSpan; }
    void setColumnSpan(int value) { assign(m_columnSpan, std::max(1, value)); }
    [[nodiscard]] QString area() const { return m_area; }
    void setArea(const QString &value) { assign(m_area, value); }
    [[nodiscard]] int justifySelf() const { return m_justifySelf; }
    void setJustifySelf(int value) { assign(m_justifySelf, value); }

private:
    int m_row = -1;
    int m_column = -1;
    int m_rowSpan = 1;
    int m_columnSpan = 1;
    QString m_area;
    int m_justifySelf = 0;
};

// AGENT-CONTRACT: a CSS grid container. Tracks come from `columns`/`rows`
// strings ("200 1fr auto", "repeat(3, minmax(80, 1fr))"), items are placed
// explicitly (Grid.row/column/area) or auto-placed in `autoFlow` order,
// implicit tracks take `autoColumns`/`autoRows`. Columns are sized from the
// children's implicit widths, widths are applied, then rows are sized from
// the resulting implicit heights, so wrapping text gets the row height it
// needs. Container implicit size is the natural track sum plus padding.
class Grid : public QQuickItem, public LayoutContainer {
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(GridAttached)
    Q_PROPERTY(QString columns READ columns WRITE setColumns NOTIFY layoutPropertyChanged)
    Q_PROPERTY(QString rows READ rows WRITE setRows NOTIFY layoutPropertyChanged)
    Q_PROPERTY(QString autoColumns READ autoColumns WRITE setAutoColumns NOTIFY layoutPropertyChanged)
    Q_PROPERTY(QString autoRows READ autoRows WRITE setAutoRows NOTIFY layoutPropertyChanged)
    Q_PROPERTY(Flow autoFlow READ autoFlow WRITE setAutoFlow NOTIFY layoutPropertyChanged)
    Q_PROPERTY(QStringList areas READ areas WRITE setAreas NOTIFY layoutPropertyChanged)
    Q_PROPERTY(Alignment justifyItems READ justifyItems WRITE setJustifyItems NOTIFY layoutPropertyChanged)
    Q_PROPERTY(Alignment alignItems READ alignItems WRITE setAlignItems NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal gap READ gap WRITE setGap NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal rowGap READ rowGap WRITE setRowGap NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal columnGap READ columnGap WRITE setColumnGap NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingLeft READ paddingLeft WRITE setPaddingLeft NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingTop READ paddingTop WRITE setPaddingTop NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingRight READ paddingRight WRITE setPaddingRight NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingBottom READ paddingBottom WRITE setPaddingBottom NOTIFY layoutPropertyChanged)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY contentSizeChanged)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Flow { Row, Column, RowDense, ColumnDense };
    Q_ENUM(Flow)
    enum Alignment { Auto, Start, End, Center, Stretch };
    Q_ENUM(Alignment)

    explicit Grid(QQuickItem *parent = nullptr);
    ~Grid() override;

    static GridAttached *qmlAttachedProperties(QObject *object);

    [[nodiscard]] QString columns() const { return m_columns; }
    void setColumns(const QString &value);
    [[nodiscard]] QString rows() const { return m_rows; }
    void setRows(const QString &value);
    [[nodiscard]] QString autoColumns() const { return m_autoColumns; }
    void setAutoColumns(const QString &value);
    [[nodiscard]] QString autoRows() const { return m_autoRows; }
    void setAutoRows(const QString &value);
    [[nodiscard]] Flow autoFlow() const { return m_autoFlow; }
    void setAutoFlow(Flow value) { assign(m_autoFlow, value); }
    [[nodiscard]] QStringList areas() const { return m_areas; }
    void setAreas(const QStringList &value) { assign(m_areas, value); }
    [[nodiscard]] Alignment justifyItems() const { return m_justifyItems; }
    void setJustifyItems(Alignment value) { assign(m_justifyItems, value); }
    [[nodiscard]] Alignment alignItems() const { return m_alignItems; }
    void setAlignItems(Alignment value) { assign(m_alignItems, value); }
    [[nodiscard]] qreal gap() const { return m_gap; }
    void setGap(qreal value) { assign(m_gap, value); }
    [[nodiscard]] qreal rowGap() const { return m_rowGap; }
    void setRowGap(qreal value) { assign(m_rowGap, value); }
    [[nodiscard]] qreal columnGap() const { return m_columnGap; }
    void setColumnGap(qreal value) { assign(m_columnGap, value); }
    [[nodiscard]] qreal padding() const { return m_padding; }
    void setPadding(qreal value) { assign(m_padding, value); }
    [[nodiscard]] qreal paddingLeft() const { return m_paddingLeft; }
    void setPaddingLeft(qreal value) { assign(m_paddingLeft, value); }
    [[nodiscard]] qreal paddingTop() const { return m_paddingTop; }
    void setPaddingTop(qreal value) { assign(m_paddingTop, value); }
    [[nodiscard]] qreal paddingRight() const { return m_paddingRight; }
    void setPaddingRight(qreal value) { assign(m_paddingRight, value); }
    [[nodiscard]] qreal paddingBottom() const { return m_paddingBottom; }
    void setPaddingBottom(qreal value) { assign(m_paddingBottom, value); }
    [[nodiscard]] int columnCount() const { return m_columnCount; }
    [[nodiscard]] int rowCount() const { return m_rowCount; }
    [[nodiscard]] qreal contentWidth() const { return m_contentWidth; }
    [[nodiscard]] qreal contentHeight() const { return m_contentHeight; }
    [[nodiscard]] QString error() const { return m_error; }

    Q_INVOKABLE void relayout();
    void invalidateLayout() override;

signals:
    void layoutPropertyChanged();
    void contentSizeChanged();
    void errorChanged();

protected:
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void updatePolish() override;

private:
    template <typename T>
    void assign(T &member, const T &value)
    {
        if (member == value) {
            return;
        }
        member = value;
        emit layoutPropertyChanged();
        invalidateLayout();
    }
    void watch(QQuickItem *child);
    void unwatch(QQuickItem *child);
    void doLayout();
    void reportError(const QString &message);
    [[nodiscard]] qreal resolvedPadding(qreal side) const { return side >= 0 ? side : m_padding; }

    QString m_columns;
    QString m_rows;
    QString m_autoColumns = QStringLiteral("auto");
    QString m_autoRows = QStringLiteral("auto");
    QVector<GridTrack> m_columnTracks;
    QVector<GridTrack> m_rowTracks;
    QVector<GridTrack> m_autoColumnTracks;
    QVector<GridTrack> m_autoRowTracks;
    Flow m_autoFlow = Row;
    QStringList m_areas;
    Alignment m_justifyItems = Stretch;
    Alignment m_alignItems = Stretch;
    qreal m_gap = 0;
    qreal m_rowGap = -1;
    qreal m_columnGap = -1;
    qreal m_padding = 0;
    qreal m_paddingLeft = -1;
    qreal m_paddingTop = -1;
    qreal m_paddingRight = -1;
    qreal m_paddingBottom = -1;
    int m_columnCount = 0;
    int m_rowCount = 0;
    qreal m_contentWidth = 0;
    qreal m_contentHeight = 0;
    QString m_error;
    bool m_inLayout = false;
    bool m_dirtyDuringLayout = false;
};

} // namespace QindaTK
