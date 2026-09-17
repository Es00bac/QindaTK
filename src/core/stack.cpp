// SPDX-License-Identifier: LGPL-3.0-or-later
#include "stack.h"

#include <QtQml/qqml.h>

#include <algorithm>

namespace QindaTK {

StackAttached::StackAttached(QObject *parent)
    : LayoutAttachedBase(parent)
{
}

void StackAttached::setInset(qreal value)
{
    m_top = m_left = m_right = m_bottom = value;
    emit changed();
    relayoutParent();
}

Stack::Stack(QQuickItem *parent)
    : QQuickItem(parent)
{
}

Stack::~Stack() = default;

StackAttached *Stack::qmlAttachedProperties(QObject *object)
{
    return new StackAttached(object);
}

void Stack::invalidateLayout()
{
    if (m_inLayout) {
        m_dirtyDuringLayout = true;
        return;
    }
    polish();
}

void Stack::relayout()
{
    doLayout();
}

void Stack::componentComplete()
{
    QQuickItem::componentComplete();
    for (QQuickItem *child : childItems()) {
        connect(child, &QQuickItem::implicitWidthChanged, this, &Stack::invalidateLayout, Qt::UniqueConnection);
        connect(child, &QQuickItem::implicitHeightChanged, this, &Stack::invalidateLayout, Qt::UniqueConnection);
        connect(child, &QQuickItem::visibleChanged, this, &Stack::invalidateLayout, Qt::UniqueConnection);
    }
    polish();
}

void Stack::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemChildAddedChange && value.item != nullptr) {
        connect(value.item, &QQuickItem::implicitWidthChanged, this, &Stack::invalidateLayout, Qt::UniqueConnection);
        connect(value.item, &QQuickItem::implicitHeightChanged, this, &Stack::invalidateLayout, Qt::UniqueConnection);
        connect(value.item, &QQuickItem::visibleChanged, this, &Stack::invalidateLayout, Qt::UniqueConnection);
        invalidateLayout();
    } else if (change == ItemChildRemovedChange) {
        if (value.item != nullptr) {
            disconnect(value.item, nullptr, this, nullptr);
        }
        invalidateLayout();
    }
}

void Stack::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateLayout();
    }
}

void Stack::updatePolish()
{
    doLayout();
}

void Stack::doLayout()
{
    if (!isComponentComplete() || m_inLayout) {
        return;
    }
    m_inLayout = true;
    const qreal padL = resolvedPadding(m_paddingLeft);
    const qreal padT = resolvedPadding(m_paddingTop);
    const qreal padR = resolvedPadding(m_paddingRight);
    const qreal padB = resolvedPadding(m_paddingBottom);
    const qreal innerW = std::max<qreal>(width() - padL - padR, 0);
    const qreal innerH = std::max<qreal>(height() - padT - padB, 0);
    qreal implicitW = 0;
    qreal implicitH = 0;
    for (QQuickItem *child : childItems()) {
        auto *a = qobject_cast<StackAttached *>(qmlAttachedPropertiesObject<Stack>(child, false));
        if (!child->isVisible() || child->inherits("QQuickRepeater") || (a && a->ignore())) {
            continue;
        }
        const qreal minW = a ? a->minWidth() : 0;
        const qreal maxW = a ? a->maxWidth() : LayoutAttachedBase::unbounded;
        const qreal minH = a ? a->minHeight() : 0;
        const qreal maxH = a ? a->maxHeight() : LayoutAttachedBase::unbounded;
        const qreal top = a ? a->top() : NAN;
        const qreal left = a ? a->left() : NAN;
        const qreal right = a ? a->right() : NAN;
        const qreal bottom = a ? a->bottom() : NAN;
        const bool fill = a == nullptr || (a->fillSet() ? a->fill() : !a->anyInset());
        const qreal implicitChildW = std::clamp(child->implicitWidth(), minW, maxW);
        const qreal implicitChildH = std::clamp(child->implicitHeight(), minH, maxH);

        qreal x = 0;
        qreal w = implicitChildW;
        const bool hasLeft = !std::isnan(left);
        const bool hasRight = !std::isnan(right);
        if (hasLeft && hasRight) {
            x = left;
            w = std::clamp(innerW - left - right, minW, maxW);
        } else if (hasLeft) {
            x = left;
        } else if (hasRight) {
            x = innerW - right - w;
        } else if (fill) {
            w = std::clamp(innerW, minW, maxW);
        } else if (a && a->centerX()) {
            x = (innerW - w) / 2;
        }
        qreal y = 0;
        qreal h = implicitChildH;
        const bool hasTop = !std::isnan(top);
        const bool hasBottom = !std::isnan(bottom);
        if (hasTop && hasBottom) {
            y = top;
            h = std::clamp(innerH - top - bottom, minH, maxH);
        } else if (hasTop) {
            y = top;
        } else if (hasBottom) {
            y = innerH - bottom - h;
        } else if (fill) {
            h = std::clamp(innerH, minH, maxH);
        } else if (a && a->centerY()) {
            y = (innerH - h) / 2;
        }
        child->setPosition(QPointF(padL + x, padT + y));
        child->setSize(QSizeF(std::max<qreal>(w, 0), std::max<qreal>(h, 0)));
        implicitW = std::max(implicitW, implicitChildW + (hasLeft ? left : 0) + (hasRight ? right : 0));
        implicitH = std::max(implicitH, implicitChildH + (hasTop ? top : 0) + (hasBottom ? bottom : 0));
    }
    setImplicitSize(implicitW + padL + padR, implicitH + padT + padB);
    m_inLayout = false;
    if (m_dirtyDuringLayout) {
        m_dirtyDuringLayout = false;
        polish();
    }
}

} // namespace QindaTK
