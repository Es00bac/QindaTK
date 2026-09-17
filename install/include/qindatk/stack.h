// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "layout_attached.h"

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <cmath>

namespace QindaTK {

class StackAttached : public LayoutAttachedBase {
    Q_OBJECT
    QML_ANONYMOUS
    // Insets from the container's padding box; NaN (the default) means
    // unset, as CSS `auto`. Two opposite insets stretch the child between
    // them; one inset keeps the child's implicit size on that axis.
    Q_PROPERTY(qreal top READ top WRITE setTop NOTIFY changed)
    Q_PROPERTY(qreal left READ left WRITE setLeft NOTIFY changed)
    Q_PROPERTY(qreal right READ right WRITE setRight NOTIFY changed)
    Q_PROPERTY(qreal bottom READ bottom WRITE setBottom NOTIFY changed)
    Q_PROPERTY(qreal inset READ inset WRITE setInset NOTIFY changed)
    // Fill the container (default when no inset is set). With insets set,
    // `fill: true` stretches on the axes that have no inset.
    Q_PROPERTY(bool fill READ fill WRITE setFill NOTIFY changed)
    // Centre on an axis that has no inset and no fill.
    Q_PROPERTY(bool centerX READ centerX WRITE setCenterX NOTIFY changed)
    Q_PROPERTY(bool centerY READ centerY WRITE setCenterY NOTIFY changed)

public:
    explicit StackAttached(QObject *parent = nullptr);

    [[nodiscard]] qreal top() const { return m_top; }
    void setTop(qreal value) { assignInset(m_top, value); }
    [[nodiscard]] qreal left() const { return m_left; }
    void setLeft(qreal value) { assignInset(m_left, value); }
    [[nodiscard]] qreal right() const { return m_right; }
    void setRight(qreal value) { assignInset(m_right, value); }
    [[nodiscard]] qreal bottom() const { return m_bottom; }
    void setBottom(qreal value) { assignInset(m_bottom, value); }
    [[nodiscard]] qreal inset() const { return m_top; }
    void setInset(qreal value);
    [[nodiscard]] bool fill() const { return m_fill; }
    void setFill(bool value) { m_fillSet = true; assign(m_fill, value); }
    [[nodiscard]] bool fillSet() const { return m_fillSet; }
    [[nodiscard]] bool centerX() const { return m_centerX; }
    void setCenterX(bool value) { assign(m_centerX, value); }
    [[nodiscard]] bool centerY() const { return m_centerY; }
    void setCenterY(bool value) { assign(m_centerY, value); }
    [[nodiscard]] bool anyInset() const
    {
        return !std::isnan(m_top) || !std::isnan(m_left) || !std::isnan(m_right) || !std::isnan(m_bottom);
    }

private:
    void assignInset(qreal &member, qreal value)
    {
        if ((std::isnan(member) && std::isnan(value)) || member == value) {
            return;
        }
        member = value;
        emit changed();
        relayoutParent();
    }
    qreal m_top = NAN;
    qreal m_left = NAN;
    qreal m_right = NAN;
    qreal m_bottom = NAN;
    bool m_fill = false;
    bool m_fillSet = false;
    bool m_centerX = false;
    bool m_centerY = false;
};

// AGENT-CONTRACT: the CSS `position: absolute` container. Children stack in
// declaration (z) order; each fills the padding box unless it sets insets
// (Stack.top/left/right/bottom/inset), a centre, or `Stack.fill: false`.
// Implicit size is the largest child implicit size plus its insets and the
// padding, so a Stack of overlays reports the size of its content layer.
class Stack : public QQuickItem, public LayoutContainer {
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(StackAttached)
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingLeft READ paddingLeft WRITE setPaddingLeft NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingTop READ paddingTop WRITE setPaddingTop NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingRight READ paddingRight WRITE setPaddingRight NOTIFY layoutPropertyChanged)
    Q_PROPERTY(qreal paddingBottom READ paddingBottom WRITE setPaddingBottom NOTIFY layoutPropertyChanged)

public:
    explicit Stack(QQuickItem *parent = nullptr);
    ~Stack() override;

    static StackAttached *qmlAttachedProperties(QObject *object);

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

    Q_INVOKABLE void relayout();
    void invalidateLayout() override;

signals:
    void layoutPropertyChanged();

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
    void doLayout();
    [[nodiscard]] qreal resolvedPadding(qreal side) const { return side >= 0 ? side : m_padding; }

    qreal m_padding = 0;
    qreal m_paddingLeft = -1;
    qreal m_paddingTop = -1;
    qreal m_paddingRight = -1;
    qreal m_paddingBottom = -1;
    bool m_inLayout = false;
    bool m_dirtyDuringLayout = false;
};

} // namespace QindaTK
