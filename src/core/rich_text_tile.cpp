// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_tile.h"
#include "rich_text_area.h"

#include <QPainter>

namespace QindaTK {

RichTextTile::RichTextTile(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
}

void RichTextTile::setArea(RichTextArea *area)
{
    if (m_area == area) return;
    if (m_area) disconnect(m_area, nullptr, this, nullptr);
    m_area = area;
    if (m_area) {
        connect(m_area, &RichTextArea::repaintRequested, this, &RichTextTile::onRepaintRequested);
        connect(m_area, &QObject::destroyed, this, [this] { update(); });
    }
    emit areaChanged();
    update();
}

void RichTextTile::setPageIndex(int page)
{
    if (m_page == page) return;
    m_page = page;
    emit pageIndexChanged();
    update();
}

void RichTextTile::setSourceRect(const QRectF &rect)
{
    if (m_source == rect) return;
    m_source = rect;
    emit sourceRectChanged();
    update();
}

void RichTextTile::setDecorations(bool on)
{
    if (m_decorations == on) return;
    m_decorations = on;
    emit decorationsChanged();
    update();
}

void RichTextTile::setPaper(bool on)
{
    if (m_paper == on) return;
    m_paper = on;
    emit decorationsChanged();
    update();
}

void RichTextTile::onRepaintRequested(int firstPage)
{
    if (firstPage < 0 || m_page >= firstPage) update();
}

void RichTextTile::paint(QPainter *painter)
{
    if (!m_area || !m_area->layout() || m_page < 0 || m_page >= m_area->pageCount()) return;
    const QSizeF page = m_area->pageSize();
    const QRectF source = m_source.isEmpty() ? QRectF(QPointF(0, 0), page) : m_source;
    if (source.width() <= 0 || source.height() <= 0 || width() <= 0 || height() <= 0) return;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    if (m_paper) painter->fillRect(QRectF(0, 0, width(), height()), m_area->property("pageColor").value<QColor>());
    painter->save();
    painter->scale(width() / source.width(), height() / source.height());
    painter->translate(-source.topLeft());
    m_area->layout()->paintPage(painter, m_page, m_area->paintContext(m_decorations), source);
    painter->restore();
    ++m_paintCount;
    emit painted();
}

} // namespace QindaTK
