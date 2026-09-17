// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "layout_attached.h"

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class FlexAttached : public LayoutAttachedBase {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(qreal grow READ grow WRITE setGrow NOTIFY changed)
    Q_PROPERTY(qreal shrink READ shrink WRITE setShrink NOTIFY changed)
    // Main-axis start size in pixels; -1 means "auto" (the implicit size).
    Q_PROPERTY(qreal basis READ basis WRITE setBasis NOTIFY changed)
    // CSS `flex: n` shorthand: grow n, shrink 1, basis 0.
    Q_PROPERTY(qreal flex READ flex WRITE setFlex NOTIFY changed)

public:
    explicit FlexAttached(QObject *parent = nullptr);

    [[nodiscard]] qreal grow() const { return m_grow; }
    void setGrow(qreal value) { assign(m_grow, value); }
    [[nodiscard]] qreal shrink() const { return m_shrink; }
    void setShrink(qreal value) { assign(m_shrink, value); }
    [[nodiscard]] qreal basis() const { return m_basis; }
    void setBasis(qreal value) { assign(m_basis, value); }
    [[nodiscard]] qreal flex() const { return m_grow; }
    void setFlex(qreal value);

private:
    qreal m_grow = 0;
    qreal m_shrink = 1;
    qreal m_basis = -1;
};

// AGENT-CONTRACT: a CSS flexbox container (CSS Flexible Box Layout Level 1,
// single-pass free-space resolution with min/max freezing). Children are
// positioned and sized by the container; it never reads their width/height,
// only implicit sizes and Flex.* attached values, so a child that binds its
// own width does not fight the layout. Container implicit size is the
// content's max-content size plus padding, so nested Flex/Grid/Box chains
// size bottom-up like HTML.
class Flex : public QQuickItem, public LayoutContainer {
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(FlexAttached)
    Q_PROPERTY(Direction direction READ direction WRITE setDirection NOTIFY layoutPropertyChanged)
    Q_PROPERTY(WrapMode wrap READ wrap WRITE setWrap NOTIFY layoutPropertyChanged)
    Q_PROPERTY(Alignment justify READ justify WRITE setJustify NOTIFY layoutPropertyChanged)
    Q_PROPERTY(Alignment align READ align WRITE setAlign NOTIFY layoutPropertyChanged)
    Q_PROPERTY(Alignment alignContent READ alignContent WRITE setAlignContent NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal gap READ gap WRITE setGap NOTIFY layoutPropertyChanged)
    // -1 inherits `gap` / `padding`.
    Q_PROPERTY(qreal rowGap READ rowGap WRITE setRowGap NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal columnGap READ columnGap WRITE setColumnGap NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingLeft READ paddingLeft WRITE setPaddingLeft NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingTop READ paddingTop WRITE setPaddingTop NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingRight READ paddingRight WRITE setPaddingRight NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingBottom READ paddingBottom WRITE setPaddingBottom NOTIFY layoutPropertyChanged)
    // Extent of the laid-out content including padding (for Scroll).
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY contentSizeChanged)

public:
    enum Direction { Row, RowReverse, Column, ColumnReverse };
    Q_ENUM(Direction)
    enum WrapMode { NoWrap, Wrap, WrapReverse };
    Q_ENUM(WrapMode)
    enum Alignment { Auto, Start, End, Center, Stretch, SpaceBetween, SpaceAround, SpaceEvenly, Baseline };
    Q_ENUM(Alignment)

    explicit Flex(QQuickItem *parent = nullptr);
    ~Flex() override;

    static FlexAttached *qmlAttachedProperties(QObject *object);

    [[nodiscard]] Direction direction() const { return m_direction; }
    void setDirection(Direction value) { assign(m_direction, value); }
    [[nodiscard]] WrapMode wrap() const { return m_wrap; }
    void setWrap(WrapMode value) { assign(m_wrap, value); }
    [[nodiscard]] Alignment justify() const { return m_justify; }
    void setJustify(Alignment value) { assign(m_justify, value); }
    [[nodiscard]] Alignment align() const { return m_align; }
    void setAlign(Alignment value) { assign(m_align, value); }
    [[nodiscard]] Alignment alignContent() const { return m_alignContent; }
    void setAlignContent(Alignment value) { assign(m_alignContent, value); }
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
    [[nodiscard]] qreal contentWidth() const { return m_contentWidth; }
    [[nodiscard]] qreal contentHeight() const { return m_contentHeight; }
    [[nodiscard]] int lineCount() const { return m_lineCount; }

    // Runs the layout now instead of at the next polish (tests, measuring).
    Q_INVOKABLE void relayout();

    void invalidateLayout() override;

signals:
    void layoutPropertyChanged();
    void contentSizeChanged();

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
    [[nodiscard]] qreal resolvedPadding(qreal side) const { return side >= 0 ? side : m_padding; }

    Direction m_direction = Row;
    WrapMode m_wrap = NoWrap;
    Alignment m_justify = Start;
    Alignment m_align = Stretch;
    Alignment m_alignContent = Stretch;
    qreal m_gap = 0;
    qreal m_rowGap = -1;
    qreal m_columnGap = -1;
    qreal m_padding = 0;
    qreal m_paddingLeft = -1;
    qreal m_paddingTop = -1;
    qreal m_paddingRight = -1;
    qreal m_paddingBottom = -1;
    qreal m_contentWidth = 0;
    qreal m_contentHeight = 0;
    int m_lineCount = 0;
    bool m_inLayout = false;
    bool m_dirtyDuringLayout = false;
};

} // namespace QindaTK
