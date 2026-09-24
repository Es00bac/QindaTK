// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_tile_source.h"
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QIODevice>
#include <QStandardPaths>
#include <QUrl>

namespace QindaTK {
struct GeographicTileSource::NetworkState {
    QNetworkAccessManager manager;
    QNetworkDiskCache *diskCache;
    QHash<QString, QPointer<QNetworkReply>> replies;
    NetworkState()
    {
        diskCache = new QNetworkDiskCache(&manager);
        manager.setCache(diskCache);
    }
};

GeographicTileSource::GeographicTileSource(QObject *parent) : QObject(parent)
{
    m_cache.setMaxCost(m_cacheLimitBytes / 1024);
    m_diskCacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                               + QStringLiteral("/qindatk-geographic-tiles");
}

GeographicTileSource::~GeographicTileSource()
{ cancelNetworkRequests({}); }

void GeographicTileSource::configureDiskCache()
{
    if (!m_network) m_network = std::make_unique<NetworkState>();
    m_network->diskCache->setCacheDirectory(m_diskCacheDirectory);
    m_network->diskCache->setMaximumCacheSize(m_diskCacheLimitBytes);
    emit diskCacheChanged();
}

void GeographicTileSource::setDiskCacheDirectory(const QString &directory)
{
    if (directory.isEmpty() || directory == m_diskCacheDirectory) return;
    m_diskCacheDirectory = directory;
    if (m_network) configureDiskCache();
    else emit diskCacheChanged();
}

void GeographicTileSource::setDiskCacheLimitBytes(qint64 bytes)
{
    bytes = qBound<qint64>(1024 * 1024, bytes, 1024LL * 1024 * 1024);
    if (bytes == m_diskCacheLimitBytes) return;
    m_diskCacheLimitBytes = bytes;
    if (m_network) configureDiskCache();
    else emit diskCacheChanged();
}

qint64 GeographicTileSource::diskCacheBytes() const
{ return m_network ? m_network->diskCache->cacheSize() : 0; }

QImage GeographicTileSource::cachedNetworkImage(const QString &id)
{
    if (!m_network) return {};
    const auto parts = id.split(QLatin1Char('/'));
    if (parts.size() != 3) return {};
    QString url = m_urlTemplate;
    url.replace(QStringLiteral("{z}"), parts[0])
       .replace(QStringLiteral("{x}"), parts[1])
       .replace(QStringLiteral("{y}"), parts[2]);
    std::unique_ptr<QIODevice> data(m_network->diskCache->data(QUrl(url)));
    if (!data) return {};
    const auto bytes = data->readAll();
    if (bytes.size() > 8 * 1024 * 1024) return {};
    const QImage image = QImage::fromData(bytes);
    return image.size() == QSize(m_tileSize, m_tileSize) ? image : QImage{};
}

void GeographicTileSource::clearDiskCache()
{
    if (m_network) m_network->diskCache->clear();
    emit diskCacheChanged();
}

bool GeographicTileSource::useNetworkTemplate(const QString &urlTemplate,
                                               const QString &attribution,
                                               int minZoom, int maxZoom, int tileSize)
{
    QString example = urlTemplate;
    if (!example.contains(QStringLiteral("{z}")) ||
        !example.contains(QStringLiteral("{x}")) ||
        !example.contains(QStringLiteral("{y}"))) {
        m_lastError = QStringLiteral("Network XYZ template requires {z}, {x} and {y}");
        emit changed(); return false;
    }
    example.replace(QStringLiteral("{z}"), QStringLiteral("0"))
           .replace(QStringLiteral("{x}"), QStringLiteral("0"))
           .replace(QStringLiteral("{y}"), QStringLiteral("0"));
    const QUrl probe(example);
    if (!probe.isValid() || probe.isRelative() || probe.host().isEmpty() ||
        (probe.scheme() != QLatin1String("http") && probe.scheme() != QLatin1String("https")) ||
        attribution.trimmed().isEmpty() || minZoom < 0 || maxZoom > 18 || minZoom > maxZoom ||
        tileSize < 64 || tileSize > 1024) {
        m_lastError = QStringLiteral("Invalid network XYZ source or missing attribution");
        emit changed(); return false;
    }
    cancelNetworkRequests({});
    m_paths.clear(); m_cache.clear(); m_pending.clear(); m_failed.clear();
    m_urlTemplate = urlTemplate; m_attribution = attribution.trimmed();
    m_minZoom = minZoom; m_maxZoom = maxZoom; m_tileSize = tileSize;
    m_lastError.clear();
    configureDiskCache();
    emit changed(); emit cacheChanged();
    setVisibleTiles(m_visible);
    return true;
}

void GeographicTileSource::cancelNetworkRequests(const QSet<QString> &keep)
{
    if (!m_network) return;
    for (auto it = m_network->replies.begin(); it != m_network->replies.end();) {
        if (keep.contains(it.key())) { ++it; continue; }
        const QString id = it.key();
        const QPointer<QNetworkReply> reply = it.value();
        it = m_network->replies.erase(it);
        m_pending.remove(id);
        if (reply) reply->abort();
    }
}

void GeographicTileSource::requestNetwork(const QString &id, quint64 serial)
{
    const auto parts = id.split(QLatin1Char('/'));
    if (parts.size() != 3 || !m_network) { finishTile(id, serial, {}); return; }
    QString url = m_urlTemplate;
    url.replace(QStringLiteral("{z}"), parts[0])
       .replace(QStringLiteral("{x}"), parts[1])
       .replace(QStringLiteral("{y}"), parts[2]);
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, true);
    request.setTransferTimeout(10000);
    QNetworkReply *reply = m_network->manager.get(request);
    reply->setReadBufferSize(8 * 1024 * 1024);
    m_network->replies.insert(id, reply);
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 8 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, serial] {
        if (m_network->replies.value(id) == reply) m_network->replies.remove(id);
        QImage image;
        if (reply->error() == QNetworkReply::NoError &&
            reply->bytesAvailable() <= 8 * 1024 * 1024) {
            image = QImage::fromData(reply->readAll());
            if (image.size() != QSize(m_tileSize, m_tileSize)) image = {};
        }
        finishTile(id, serial, std::move(image));
        reply->deleteLater();
        emit diskCacheChanged();
    });
}
}
