// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_tile_source.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>
#include <utility>

namespace QindaTK {
QString GeographicTileSource::key(int z, int x, int y)
{ return QString::number(z) + QLatin1Char('/') + QString::number(x) + QLatin1Char('/') + QString::number(y); }

void GeographicTileSource::setCacheLimitBytes(int bytes)
{
    bytes = qBound(1024 * 1024, bytes, 256 * 1024 * 1024);
    if (bytes == m_cacheLimitBytes) return;
    m_cacheLimitBytes = bytes;
    m_cache.setMaxCost(bytes / 1024);
    emit cacheChanged();
}

void GeographicTileSource::clear()
{
    cancelNetworkRequests({});
    m_urlTemplate.clear();
    m_paths.clear(); m_cache.clear(); m_visible.clear(); m_pending.clear(); m_failed.clear();
    m_attribution.clear(); m_lastError.clear();
    m_minZoom = 0; m_maxZoom = 18; m_tileSize = 256;
    emit changed(); emit cacheChanged();
}

bool GeographicTileSource::loadManifest(const QString &path)
{
    const QUrl url(path);
    if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()) {
        m_lastError = QStringLiteral("Only local tile manifests are supported"); emit changed(); return false;
    }
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = file.errorString(); emit changed(); return false;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto manifest = doc.object();
    const int size = manifest.value("tileSize").toInt(256);
    const int min = manifest.value("minZoom").toInt(0);
    const int max = manifest.value("maxZoom").toInt(18);
    const auto tiles = manifest.value("tiles").toArray();
    if (parse.error != QJsonParseError::NoError || !doc.isObject() ||
        size < 64 || size > 1024 || min < 0 || max > 18 || min > max || tiles.isEmpty()) {
        m_lastError = QStringLiteral("Invalid local XYZ tile manifest"); emit changed(); return false;
    }
    QHash<QString, QString> paths;
    const QDir base(QFileInfo(local).absolutePath());
    for (const auto &item : tiles) {
        const auto tile = item.toObject();
        const int z = tile.value("z").toInt(-1);
        const int x = tile.value("x").toInt(-1);
        const int y = tile.value("y").toInt(-1);
        const QString relative = tile.value("file").toString();
        const qint64 count = z >= 0 && z <= 18 ? (1LL << z) : 0;
        if (z < min || z > max || x < 0 || x >= count || y < 0 || y >= count ||
            relative.isEmpty() || QFileInfo(relative).isAbsolute() ||
            relative.split('/').contains(QStringLiteral(".."))) {
            m_lastError = QStringLiteral("Invalid tile coordinate or path"); emit changed(); return false;
        }
        const QString id = key(z,x,y);
        if (paths.contains(id)) {
            m_lastError = QStringLiteral("Duplicate XYZ tile"); emit changed(); return false;
        }
        paths.insert(id, base.filePath(relative));
    }
    // Failed replacement leaves the previous source and cache intact.
    cancelNetworkRequests({});
    m_urlTemplate.clear();
    m_paths = std::move(paths); m_cache.clear(); m_pending.clear(); m_failed.clear();
    m_attribution = manifest.value("attribution").toString();
    m_minZoom = min; m_maxZoom = max; m_tileSize = size; m_lastError.clear();
    emit changed(); emit cacheChanged();
    setVisibleTiles(m_visible);
    return true;
}

int GeographicTileSource::tileState(const QString &id) const
{
    if (!m_paths.contains(id) && m_urlTemplate.isEmpty()) return 0;
    if (m_cache.contains(id)) return 2;
    if (m_failed.contains(id)) return 3;
    return 1;
}

QImage GeographicTileSource::tileImage(const QString &id)
{
    auto *image = m_cache.object(id);
    return image ? *image : QImage();
}

QImage GeographicTileSource::imageForExport(const QString &id)
{
    if (auto *cached = m_cache.object(id)) return *cached;
    if (!m_urlTemplate.isEmpty()) return cachedNetworkImage(id);
    if (!m_paths.contains(id)) return {};
    QImageReader reader(m_paths.value(id));
    reader.setAutoTransform(true);
    QImage image = reader.read();
    return image.size() == QSize(m_tileSize, m_tileSize) ? image : QImage{};
}

void GeographicTileSource::setVisibleTiles(const QSet<QString> &keys)
{
    if (!m_urlTemplate.isEmpty())
        for (const auto &id : std::as_const(m_visible))
            if (!keys.contains(id)) m_failed.remove(id); // retry a failed network tile on return
    m_visible = keys;
    cancelNetworkRequests(keys);
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (!keys.contains(it.key())) it = m_pending.erase(it);
        else ++it;
    }
    for (const auto &id : keys)
        if ((m_paths.contains(id) || !m_urlTemplate.isEmpty()) &&
            !m_cache.contains(id) && !m_failed.contains(id) && !m_pending.contains(id))
            request(id);
}

void GeographicTileSource::request(const QString &id)
{
    const quint64 serial = ++m_requestSerial;
    m_pending.insert(id, serial);
    if (!m_urlTemplate.isEmpty()) { requestNetwork(id, serial); return; }
    const auto path = m_paths.value(id);
    const QPointer<GeographicTileSource> guard(this);
    QThreadPool::globalInstance()->start([guard, id, path, serial, expectedSize = m_tileSize] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage image = reader.read();
        if (!image.isNull() && image.size() != QSize(expectedSize, expectedSize)) image = {};
        QMetaObject::invokeMethod(QCoreApplication::instance(),
            [guard, id, serial, image = std::move(image)]() mutable {
                if (guard) guard->finishTile(id, serial, std::move(image));
            }, Qt::QueuedConnection);
    });
}

void GeographicTileSource::finishTile(const QString &id, quint64 serial, QImage image)
{
    if (m_pending.value(id) != serial || !m_visible.contains(id)) return;
    m_pending.remove(id);
    if (image.isNull()) m_failed.insert(id);
    else {
        const int kib = static_cast<int>(qMax<qsizetype>(1, (image.sizeInBytes() + 1023) / 1024));
        m_cache.insert(id, new QImage(std::move(image)), kib);
        emit cacheChanged();
    }
    emit tileReady(id);
}
}
