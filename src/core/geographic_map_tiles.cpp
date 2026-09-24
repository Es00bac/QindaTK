// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_map_canvas.h"
#include "geographic_tile_source.h"
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK {
void GeographicMapCanvas::setTileSource(GeographicTileSource *source)
{
    if (m_tileSource == source) return;
    if (m_tileSource) disconnect(m_tileSource, nullptr, this, nullptr);
    m_tileSource = source;
    if (source) {
        connect(source, &GeographicTileSource::changed, this, &GeographicMapCanvas::refreshTiles);
        connect(source, &GeographicTileSource::tileReady, this, [this] { refreshTiles(); });
        connect(source, &QObject::destroyed, this, [this] {
            m_tileSource = nullptr;
            emit tileSourceChanged();
            refreshTiles();
        });
    }
    emit tileSourceChanged();
    refreshTiles();
}

void GeographicMapCanvas::geometryChange(const QRectF &next, const QRectF &previous)
{
    QQuickPaintedItem::geometryChange(next, previous);
    if (next.size() != previous.size()) refreshTiles();
}

void GeographicMapCanvas::refreshTiles()
{
    m_visibleTiles.clear();
    QString nextStatus = QStringLiteral("unavailable");
    if (m_tileSource && m_tileSource->loaded() && width() > 0 && height() > 0) {
        const int z = std::clamp(static_cast<int>(std::floor(m_zoom)),
                                 m_tileSource->minZoom(), m_tileSource->maxZoom());
        const int count = 1 << z;
        const double tilePixels = 256.0 * std::pow(2.0, m_zoom - z);
        const double worldPixels = tilePixels * count;
        const double left = m_centerX * worldPixels - width() / 2;
        const double top = m_centerY * worldPixels - height() / 2;
        const int firstX = static_cast<int>(std::floor(left / tilePixels));
        const int lastX = static_cast<int>(std::floor((left + width()) / tilePixels));
        const int firstY = std::max(0, static_cast<int>(std::floor(top / tilePixels)));
        const int lastY = std::min(count - 1, static_cast<int>(std::floor((top + height()) / tilePixels)));
        const qint64 drawCount = qint64(lastX - firstX + 1) * qint64(lastY - firstY + 1);
        if (drawCount > 512) {
            m_tileSource->setVisibleTiles({});
            nextStatus = QStringLiteral("zoom outside source range");
            if (m_tileStatus != nextStatus) { m_tileStatus = nextStatus; emit tileStatusChanged(); }
            update();
            return;
        }
        QSet<QString> keys;
        // At zoom 0, one world tile can be repeated across a wide viewport.
        for (int y = firstY; y <= lastY; ++y)
            for (int rawX = firstX; rawX <= lastX; ++rawX) {
                const int x = ((rawX % count) + count) % count;
                const QString id = GeographicTileSource::key(z, x, y);
                keys.insert(id);
                m_visibleTiles.append({id, QRectF(rawX * tilePixels - left,
                                                  y * tilePixels - top, tilePixels, tilePixels), {}});
            }
        m_tileSource->setVisibleTiles(keys);
        for (auto &tile : m_visibleTiles) tile.image = m_tileSource->tileImage(tile.key);
        int ready = 0, missing = 0, failed = 0;
        for (const auto &id : keys) {
            switch (m_tileSource->tileState(id)) {
            case 0: ++missing; break;
            case 2: ++ready; break;
            case 3: ++failed; break;
            default: break;
            }
        }
        nextStatus = QStringLiteral("%1/%2 ready, %3 missing, %4 failed")
                         .arg(ready).arg(keys.size()).arg(missing).arg(failed);
    } else if (m_tileSource) {
        m_tileSource->setVisibleTiles({});
    }
    if (m_tileStatus != nextStatus) { m_tileStatus = nextStatus; emit tileStatusChanged(); }
    update();
}

void GeographicMapCanvas::paintTiles(QPainter *painter)
{
    if (!m_tileSource) return;
    painter->save();
    painter->setClipRect(boundingRect());
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    for (const auto &tile : m_visibleTiles)
        if (!tile.image.isNull()) painter->drawImage(tile.rect, tile.image);
    painter->restore();
}
}
