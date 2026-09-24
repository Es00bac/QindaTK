// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_map_canvas.h"
#include <QPainter>
#include <QSaveFile>
#include <QHash>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace QindaTK {
bool GeographicMapCanvas::exportPng(const QString &path, int pixelWidth, int pixelHeight,
                                     double longitude, double latitude, double zoomLevel)
{
    const auto fail = [this](const QString &message) {
        m_exportError = message; emit exportErrorChanged(); return false;
    };
    const QUrl url(path);
    if (path.isEmpty() || (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()))
        return fail(QStringLiteral("Export requires a local path"));
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    if (pixelWidth < 1 || pixelHeight < 1 || pixelWidth > 8192 || pixelHeight > 8192 ||
        !std::isfinite(longitude) || longitude < -180 || longitude > 180 ||
        !std::isfinite(latitude) || latitude < -90 || latitude > 90 ||
        !std::isfinite(zoomLevel) || zoomLevel < 0 || zoomLevel > 18)
        return fail(QStringLiteral("Invalid export dimensions or WGS84 viewport"));
    if (m_tileSource && m_tileSource->loaded() && !mercatorProjection())
        return fail(QStringLiteral("XYZ tiles require EPSG:3857 display projection"));

    GeographicMapCanvas snapshot;
    snapshot.setWidth(pixelWidth);
    snapshot.setHeight(pixelHeight);
    snapshot.m_centerX = wrap((longitude + 180.0) / 360.0);
    snapshot.m_projection = m_projection;
    snapshot.m_centerY = snapshot.projectDisplay(0, latitude).y();
    snapshot.m_zoom = zoomLevel;
    snapshot.m_model = m_model;
    snapshot.m_tileSource = m_tileSource; // Do not replace the live source's visible requests.
    snapshot.m_selectedId = m_selectedId;
    snapshot.m_background = m_background;
    snapshot.m_feature = m_feature;
    snapshot.m_selection = m_selection;
    snapshot.m_grid = m_grid;
    snapshot.m_pointRadius = m_pointRadius;
    snapshot.m_lineWidth = m_lineWidth;
    snapshot.m_gridWidth = m_gridWidth;
    snapshot.m_hitRadius = m_hitRadius;
    snapshot.m_fillOpacity = m_fillOpacity;
    snapshot.m_selectedFillOpacity = m_selectedFillOpacity;

    if (m_tileSource && m_tileSource->loaded()) {
        QHash<QString, QImage> decoded;
        const int z = std::clamp(static_cast<int>(std::floor(zoomLevel)),
                                 m_tileSource->minZoom(), m_tileSource->maxZoom());
        const int count = 1 << z;
        const double tilePixels = 256.0 * std::pow(2.0, zoomLevel - z);
        const double worldPixels = tilePixels * count;
        const double left = snapshot.m_centerX * worldPixels - pixelWidth / 2.0;
        const double top = snapshot.m_centerY * worldPixels - pixelHeight / 2.0;
        const int firstX = static_cast<int>(std::floor(left / tilePixels));
        const int lastX = static_cast<int>(std::ceil((left + pixelWidth) / tilePixels)) - 1;
        const int firstY = std::max(0, static_cast<int>(std::floor(top / tilePixels)));
        const int lastY = std::min(count - 1, static_cast<int>(std::ceil((top + pixelHeight) / tilePixels)) - 1);
        const qint64 copies = qint64(lastX - firstX + 1) * qint64(std::max(0, lastY - firstY + 1));
        if (copies > 512) return fail(QStringLiteral("Export viewport exceeds 512 tile copies"));
        for (int y = firstY; y <= lastY; ++y)
            for (int rawX = firstX; rawX <= lastX; ++rawX) {
                const int x = ((rawX % count) + count) % count;
                const QString key = GeographicTileSource::key(z, x, y);
                if (!decoded.contains(key)) decoded.insert(key, m_tileSource->imageForExport(key));
                const QImage tile = decoded.value(key);
                if (tile.isNull())
                    return fail(QStringLiteral("Missing export tile %1 (network tiles must already be cached)").arg(key));
                snapshot.m_visibleTiles.append({key, QRectF(rawX * tilePixels - left,
                                                               y * tilePixels - top,
                                                               tilePixels, tilePixels), tile});
            }
    }

    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return fail(QStringLiteral("Could not allocate export image"));
    QStringList sources{m_model ? m_model->attribution() : QString{},
                        m_tileSource ? m_tileSource->attribution() : QString{}};
    sources.removeAll(QString{});
    const QString attribution = sources.join(QStringLiteral(" · "));
    image.setText(QStringLiteral("QindaTK/CRS"),
                  QStringLiteral("EPSG:4326 input; %1 display").arg(m_projection));
    image.setText(QStringLiteral("QindaTK/Viewport"),
                  QStringLiteral("%1x%2 lon=%3 lat=%4 zoom=%5")
                      .arg(pixelWidth).arg(pixelHeight).arg(longitude, 0, 'g', 17)
                      .arg(latitude, 0, 'g', 17).arg(zoomLevel, 0, 'g', 17));
    image.setText(QStringLiteral("QindaTK/Attribution"), attribution);
    {
        QPainter painter(&image);
        snapshot.paint(&painter);
        if (!attribution.isEmpty()) {
            painter.setClipRect(image.rect());
            const int padding = std::max(1, static_cast<int>(std::ceil(m_pointRadius)));
            const QFontMetrics fm(painter.font());
            const int width = std::min(pixelWidth, fm.horizontalAdvance(attribution) + 2 * padding);
            const QRect box(0, pixelHeight - fm.height() - 2 * padding,
                            width, fm.height() + 2 * padding);
            painter.fillRect(box, m_background);
            painter.setPen(m_feature);
            painter.drawText(box.adjusted(padding, 0, -padding, 0),
                             Qt::AlignLeft | Qt::AlignVCenter, attribution);
        }
    }
    QSaveFile file(local);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    if (!image.save(&file, "PNG") || !file.commit()) return fail(QStringLiteral("Could not write PNG export"));
    if (!m_exportError.isEmpty()) { m_exportError.clear(); emit exportErrorChanged(); }
    return true;
}
}
