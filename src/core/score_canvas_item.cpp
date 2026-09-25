// SPDX-License-Identifier: LGPL-3.0-or-later
// ScoreView's drawing surface through the one score painter (WP-067, D-179).
#include "score_canvas_item.h"

#include <QPainter>

namespace QindaTK {

ScoreCanvasItem::ScoreCanvasItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(true);
}

void ScoreCanvasItem::paint(QPainter *painter)
{
    Score::paintSnapshot(painter, m_snapshot, size(), m_style, m_selected, m_cursorX, m_maximumEvents);
}

void ScoreCanvasItem::changed()
{
    emit appearanceChanged();
    update();
}

void ScoreCanvasItem::setSnapshot(const QVariantMap &snapshot)
{
    m_snapshot = snapshot;
    changed();
}

void ScoreCanvasItem::setSelectedEventId(const QString &id)
{
    if (id == m_selected) return;
    m_selected = id;
    changed();
}

void ScoreCanvasItem::setCursorX(double x)
{
    if (qFuzzyCompare(x + 2, m_cursorX + 2)) return;
    m_cursorX = x;
    changed();
}

void ScoreCanvasItem::setMaximumEvents(int count)
{
    if (count == m_maximumEvents) return;
    m_maximumEvents = count;
    changed();
}

void ScoreCanvasItem::setFontFamily(const QString &family)
{
    if (family == m_style.fontFamily) return;
    m_style.fontFamily = family;
    changed();
}

void ScoreCanvasItem::setColor(QColor &target, const QColor &color)
{
    if (target == color) return;
    target = color;
    changed();
}

} // namespace QindaTK
