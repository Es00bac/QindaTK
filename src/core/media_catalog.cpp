// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_catalog.h"
#include "media_source_info.h"

#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>

namespace QindaTK {
namespace {
const QString kSchema = QStringLiteral("qindatk.media-catalog/1");

QString frameTimecode(qint64 frames, qint64 numerator, qint64 denominator)
{
    const qint64 fps = std::max<qint64>(1, (numerator + denominator / 2) / denominator);
    const qint64 f = frames % fps;
    const qint64 seconds = frames / fps;
    return QStringLiteral("%1:%2:%3:%4").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

QString durationText(const QVariantMap &metadata)
{
    if (!metadata.value(QStringLiteral("hasVideo")).toBool())
        return metadata.contains(QStringLiteral("durationSeconds"))
            ? QStringLiteral("%1 s").arg(metadata.value(QStringLiteral("durationSeconds")).toDouble(), 0, 'f', 3)
            : QString();
    if (!metadata.value(QStringLiteral("constantRate")).toBool())
        return QStringLiteral("%1 s VFR").arg(metadata.value(QStringLiteral("durationSeconds")).toDouble(), 0, 'f', 3);
    return frameTimecode(metadata.value(QStringLiteral("frameCount")).toString().toLongLong(),
                         metadata.value(QStringLiteral("frameRateNumerator")).toString().toLongLong(),
                         std::max<qint64>(1, metadata.value(QStringLiteral("frameRateDenominator")).toString().toLongLong()));
}

QString audioText(const QVariantMap &metadata)
{
    if (!metadata.value(QStringLiteral("hasAudio")).toBool()) return {};
    return QStringLiteral("%1 Hz · %2 ch · %3").arg(metadata.value(QStringLiteral("audioSampleRate")).toInt())
        .arg(metadata.value(QStringLiteral("audioChannels")).toInt())
        .arg(metadata.value(QStringLiteral("audioCodec")).toString());
}
} // namespace

MediaCatalog::MediaCatalog(QObject *parent)
    : QAbstractListModel(parent), m_jobs(new JobModel(this))
{
    m_jobs->setMaxConcurrent(2);
    m_jobs->setMaxQueued(4096);
    connect(m_jobs, &JobModel::jobFinished, this, &MediaCatalog::onJobFinished);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!cache.isEmpty()) m_proxyDirectory = cache + QStringLiteral("/media-proxies");
}

MediaCatalog::~MediaCatalog() = default;

int MediaCatalog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visible.size());
}

QHash<int, QByteArray> MediaCatalog::roleNames() const
{
    static const QHash<int, QByteArray> names{{AssetIdRole, "assetId"}, {ResourceIdRole, "resourceId"}, {LabelRole, "label"},
            {CaptionRole, "caption"}, {BinIdRole, "binId"}, {BinNameRole, "binName"}, {TagsRole, "tags"},
            {UriRole, "uri"}, {FileNameRole, "fileName"}, {AvailabilityRole, "availability"},
            {OnlineRole, "online"}, {MetadataStateRole, "metadataState"}, {MediaTypeRole, "mediaType"},
            {CodecRole, "codec"}, {ResolutionRole, "resolution"}, {FrameRateRole, "frameRate"},
            {FrameCountRole, "frameCount"}, {DurationRole, "duration"}, {SourceTimecodeRole, "sourceTimecode"},
            {AudioRole, "audio"}, {ProxyStatusRole, "proxyStatus"}, {ProxyResourceIdRole, "proxyResourceId"},
            {ProxyActiveRole, "proxyActive"}, {ProxyAvailableRole, "proxyAvailable"}, {ErrorRole, "error"},
            {MetadataRole, "metadata"}};
    return names;
}

QVariant MediaCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const Asset &asset = m_assets.at(m_visible.at(index.row()));
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole: return asset.label;
    case AssetIdRole: return asset.assetId;
    case ResourceIdRole: return asset.resourceId;
    case BinIdRole: return asset.binId;
    case TagsRole: return asset.tags;
    case MetadataStateRole: return asset.metadataState;
    default: break;
    }
    const QByteArray name = roleNames().value(role);
    return name.isEmpty() ? QVariant() : assetMap(asset).value(QString::fromLatin1(name));
}

QVariantMap MediaCatalog::resource(const QString &resourceId) const
{
    return m_resolver && !resourceId.isEmpty() ? m_resolver->lookup(resourceId) : QVariantMap();
}

void MediaCatalog::refreshEntry(Asset &asset) const
{
    asset.entry = resource(asset.resourceId);
}

QVariantMap MediaCatalog::assetMap(const Asset &asset) const
{
    const QVariantMap &entry = asset.entry;
    const QString uri = entry.value(QStringLiteral("uri")).toString();
    const QString availability = entry.isEmpty() ? QStringLiteral("missing")
                                                 : entry.value(QStringLiteral("availability")).toString();
    const bool online = availability != QLatin1String("missing") && asset.metadataState != QLatin1String("missing");
    const QVariantMap &metadata = asset.metadata;
    const QString duration = asset.metadataState == QLatin1String("ready") ? durationText(metadata) : QString();
    const QString resolution = metadata.value(QStringLiteral("resolution")).toString();
    QString caption;
    if (!online) caption = tr("Offline");
    else if (asset.metadataState == QLatin1String("pending")) caption = tr("Reading…");
    else if (asset.metadataState == QLatin1String("failed")) caption = tr("Unreadable");
    else {
        QStringList parts;
        for (const QString &part : {duration, resolution})
            if (!part.isEmpty()) parts.append(part);
        caption = parts.join(QStringLiteral(" · "));
    }
    const QString proxyUri = proxyUriFor(asset);
    const bool proxyAvailable = asset.proxyStatus == QLatin1String("ready");
    return {{QStringLiteral("assetId"), asset.assetId}, {QStringLiteral("resourceId"), asset.resourceId},
            {QStringLiteral("label"), asset.label}, {QStringLiteral("name"), asset.label},
            {QStringLiteral("caption"), caption}, {QStringLiteral("binId"), asset.binId},
            {QStringLiteral("binName"), binName(asset.binId)}, {QStringLiteral("tags"), asset.tags},
            {QStringLiteral("uri"), uri}, {QStringLiteral("fileName"), QFileInfo(mediaLocalPath(uri)).fileName()},
            {QStringLiteral("availability"), availability}, {QStringLiteral("online"), online},
            {QStringLiteral("metadataState"), asset.metadataState},
            {QStringLiteral("mediaType"), metadata.value(QStringLiteral("hasVideo")).toBool() ? QStringLiteral("video")
                : metadata.value(QStringLiteral("hasAudio")).toBool() ? QStringLiteral("audio") : QString()},
            {QStringLiteral("codec"), metadata.value(QStringLiteral("codec")).toString()},
            {QStringLiteral("resolution"), resolution},
            {QStringLiteral("frameRate"), metadata.value(QStringLiteral("frameRate")).toString()},
            {QStringLiteral("frameCount"), metadata.value(QStringLiteral("frameCount")).toString()},
            {QStringLiteral("duration"), duration},
            {QStringLiteral("sourceTimecode"), metadata.value(QStringLiteral("sourceTimecode")).toString()},
            {QStringLiteral("audio"), audioText(metadata)},
            {QStringLiteral("proxyStatus"), asset.proxyStatus},
            {QStringLiteral("proxyResourceId"), asset.proxyResourceId},
            {QStringLiteral("proxyActive"), asset.proxyActive && !proxyUri.isEmpty()},
            {QStringLiteral("proxyAvailable"), proxyAvailable},
            {QStringLiteral("proxyError"), asset.proxyError},
            {QStringLiteral("error"), asset.error}, {QStringLiteral("metadata"), metadata}};
}

void MediaCatalog::setResolver(ResourceResolver *resolver)
{
    if (m_resolver == resolver) return;
    if (m_resolver) disconnect(m_resolver, nullptr, this, nullptr);
    m_resolver = resolver;
    if (m_resolver)
        connect(m_resolver, &ResourceResolver::resourceChanged, this, &MediaCatalog::onResourceChanged);
    emit resolverChanged();
    for (int i = 0; i < m_assets.size(); ++i) probe(i);
}

void MediaCatalog::setBinFilter(const QString &binId)
{
    if (m_binFilter == binId) return;
    m_binFilter = binId;
    rebuildVisible();
    emit filterChanged();
}

void MediaCatalog::setSearchText(const QString &text)
{
    if (m_searchText == text) return;
    m_searchText = text;
    rebuildVisible();
    emit filterChanged();
}

void MediaCatalog::setProxyDirectory(const QString &directory)
{
    const QString path = directory.isEmpty() ? QString() : mediaLocalPath(directory);
    if (path == m_proxyDirectory) return;
    m_proxyDirectory = path;
    emit proxyDirectoryChanged();
}

QVariantList MediaCatalog::bins() const
{
    QHash<QString, int> counts;
    for (const Asset &asset : m_assets) ++counts[asset.binId];
    QVariantList list;
    for (const Bin &bin : m_bins)
        list.append(QVariantMap{{QStringLiteral("binId"), bin.binId}, {QStringLiteral("name"), bin.name},
                                {QStringLiteral("count"), counts.value(bin.binId)}});
    return list;
}

int MediaCatalog::assetIndex(const QString &assetId) const
{
    for (int i = 0; i < m_assets.size(); ++i)
        if (m_assets.at(i).assetId == assetId) return i;
    return -1;
}

int MediaCatalog::binIndex(const QString &binId) const
{
    for (int i = 0; i < m_bins.size(); ++i)
        if (m_bins.at(i).binId == binId) return i;
    return -1;
}

QString MediaCatalog::binName(const QString &binId) const
{
    const int index = binIndex(binId);
    return index >= 0 ? m_bins.at(index).name : QString();
}

bool MediaCatalog::matches(const Asset &asset) const
{
    if (!m_binFilter.isEmpty() && asset.binId != m_binFilter) return false;
    const QString query = m_searchText.trimmed();
    if (query.isEmpty()) return true;
    const QStringList fields{asset.label,
                             QFileInfo(mediaLocalPath(asset.entry.value(QStringLiteral("uri")).toString())).fileName(),
                             asset.tags.join(QLatin1Char(' ')), asset.metadata.value(QStringLiteral("codec")).toString(),
                             asset.metadata.value(QStringLiteral("sourceTimecode")).toString()};
    return fields.join(QLatin1Char('\n')).contains(query, Qt::CaseInsensitive);
}

void MediaCatalog::rebuildVisible()
{
    QVector<int> visible;
    for (int i = 0; i < m_assets.size(); ++i)
        if (matches(m_assets.at(i))) visible.append(i);
    const int before = int(m_visible.size());
    if (visible != m_visible) {
        beginResetModel();
        m_visible = visible;
        endResetModel();
    }
    if (before != m_visible.size()) emit countChanged();
}

void MediaCatalog::assetUpdated(int index)
{
    if (index < 0 || index >= m_assets.size()) return;
    // m_visible is ascending, so a row is found by binary search.
    const auto found = std::lower_bound(m_visible.cbegin(), m_visible.cend(), index);
    const int row = found != m_visible.cend() && *found == index ? int(found - m_visible.cbegin()) : -1;
    const bool visible = matches(m_assets.at(index));
    if (row >= 0 && visible) emit dataChanged(this->index(row), this->index(row));
    else if (row >= 0 || visible) rebuildVisible();
    emit assetChanged(m_assets.at(index).assetId);
}

bool MediaCatalog::fail(const QString &message)
{
    m_lastError = message;
    emit errorChanged();
    return false;
}

void MediaCatalog::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit errorChanged();
}

QString MediaCatalog::importMedia(const QString &uri, const QString &binId, const QString &label)
{
    clearError();
    const QString path = mediaLocalPath(uri);
    if (!m_resolver) return fail(tr("A resource resolver is required to import media")), QString();
    if (path.isEmpty()) return fail(tr("Media import accepts local paths and file URIs")), QString();
    if (!binId.isEmpty() && binIndex(binId) < 0) return fail(tr("Unknown bin %1").arg(binId)), QString();
    if (m_assets.size() >= kMaxAssets) return fail(tr("The catalog holds at most %1 assets").arg(kMaxAssets)), QString();
    const QString resourceId = QStringLiteral("media:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_resolver->registerResource(resourceId, QUrl::fromLocalFile(path).toString()))
        return fail(m_resolver->lastError()), QString();
    const QString assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!addAsset(assetId, resourceId, label.isEmpty() ? QFileInfo(path).fileName() : label, binId)) return {};
    return assetId;
}

bool MediaCatalog::addAsset(const QString &assetId, const QString &resourceId, const QString &label,
                            const QString &binId)
{
    clearError();
    if (assetId.trimmed().isEmpty() || assetId.size() > 256 || resourceId.isEmpty())
        return fail(tr("An asset needs an ID of 1–256 characters and a resource ID"));
    if (assetIndex(assetId) >= 0) return fail(tr("Asset %1 already exists").arg(assetId));
    if (!binId.isEmpty() && binIndex(binId) < 0) return fail(tr("Unknown bin %1").arg(binId));
    if (m_assets.size() >= kMaxAssets) return fail(tr("The catalog holds at most %1 assets").arg(kMaxAssets));
    Asset asset;
    asset.assetId = assetId;
    asset.resourceId = resourceId;
    asset.label = label.isEmpty() ? resourceId : label;
    asset.binId = binId;
    refreshEntry(asset);
    m_assets.append(asset);
    const int index = int(m_assets.size()) - 1;
    if (matches(m_assets.at(index))) {
        beginInsertRows({}, int(m_visible.size()), int(m_visible.size()));
        m_visible.append(index);
        endInsertRows();
        emit countChanged();
    }
    if (!binId.isEmpty()) emit binsChanged();
    probe(index);
    return true;
}

bool MediaCatalog::removeAsset(const QString &assetId)
{
    clearError();
    const int index = assetIndex(assetId);
    if (index < 0) return fail(tr("Unknown asset %1").arg(assetId));
    const bool binned = !m_assets.at(index).binId.isEmpty();
    beginResetModel();
    m_assets.removeAt(index);
    m_visible.clear();
    for (int i = 0; i < m_assets.size(); ++i)
        if (matches(m_assets.at(i))) m_visible.append(i);
    endResetModel();
    emit countChanged();
    if (binned) emit binsChanged();
    return true;
}

QString MediaCatalog::addBin(const QString &name)
{
    clearError();
    if (name.trimmed().isEmpty() || name.size() > 256) return fail(tr("A bin needs a name of 1–256 characters")), QString();
    if (m_bins.size() >= kMaxBins) return fail(tr("The catalog holds at most %1 bins").arg(kMaxBins)), QString();
    const QString binId = QStringLiteral("bin:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_bins.append({binId, name.trimmed()});
    emit binsChanged();
    return binId;
}

bool MediaCatalog::renameBin(const QString &binId, const QString &name)
{
    clearError();
    const int index = binIndex(binId);
    if (index < 0 || name.trimmed().isEmpty() || name.size() > 256) return fail(tr("Unknown bin or invalid name"));
    m_bins[index].name = name.trimmed();
    emit binsChanged();
    for (int i = 0; i < m_assets.size(); ++i)
        if (m_assets.at(i).binId == binId) assetUpdated(i);
    return true;
}

bool MediaCatalog::removeBin(const QString &binId)
{
    clearError();
    const int index = binIndex(binId);
    if (index < 0) return fail(tr("Unknown bin %1").arg(binId));
    m_bins.removeAt(index);
    for (Asset &asset : m_assets)
        if (asset.binId == binId) asset.binId.clear();
    if (m_binFilter == binId) {
        m_binFilter.clear();
        emit filterChanged();
    }
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_assets.size(); ++i)
        if (matches(m_assets.at(i))) m_visible.append(i);
    endResetModel();
    emit countChanged();
    emit binsChanged();
    return true;
}

bool MediaCatalog::moveToBin(const QStringList &assetIds, const QString &binId)
{
    clearError();
    if (!binId.isEmpty() && binIndex(binId) < 0) return fail(tr("Unknown bin %1").arg(binId));
    QVector<int> indices;
    for (const QString &assetId : assetIds) {
        const int index = assetIndex(assetId);
        if (index < 0) return fail(tr("Unknown asset %1").arg(assetId));
        indices.append(index);
    }
    // AGENT-GUARD: organizing never edits resourceId, the key sequences use.
    for (const int index : std::as_const(indices)) m_assets[index].binId = binId;
    rebuildVisible();
    for (const int index : std::as_const(indices)) assetUpdated(index);
    emit binsChanged();
    return true;
}

bool MediaCatalog::setLabel(const QString &assetId, const QString &label)
{
    clearError();
    const int index = assetIndex(assetId);
    if (index < 0 || label.trimmed().isEmpty() || label.size() > 1024) return fail(tr("Unknown asset or invalid label"));
    m_assets[index].label = label.trimmed();
    assetUpdated(index);
    return true;
}

bool MediaCatalog::setTags(const QString &assetId, const QStringList &tags)
{
    clearError();
    const int index = assetIndex(assetId);
    if (index < 0 || tags.size() > 64) return fail(tr("Unknown asset or more than 64 tags"));
    QStringList cleaned;
    for (const QString &tag : tags)
        if (!tag.trimmed().isEmpty() && !cleaned.contains(tag.trimmed())) cleaned.append(tag.trimmed());
    m_assets[index].tags = cleaned;
    assetUpdated(index);
    return true;
}

bool MediaCatalog::relink(const QString &assetId, const QString &uri)
{
    clearError();
    const int index = assetIndex(assetId);
    if (index < 0) return fail(tr("Unknown asset %1").arg(assetId));
    const QString path = mediaLocalPath(uri);
    if (!m_resolver || path.isEmpty()) return fail(tr("Relinking needs a resolver and a local file"));
    // The resolver changes only the location; resourceChanged re-reads metadata.
    if (!m_resolver->relink(m_assets.at(index).resourceId, QUrl::fromLocalFile(path).toString()))
        return fail(m_resolver->lastError());
    return true;
}

bool MediaCatalog::setProxyActive(const QString &assetId, bool active)
{
    clearError();
    const int index = assetIndex(assetId);
    if (index < 0) return fail(tr("Unknown asset %1").arg(assetId));
    Asset &asset = m_assets[index];
    if (active && asset.proxyStatus != QLatin1String("ready")) return fail(tr("No proxy is ready for %1").arg(asset.label));
    if (asset.proxyActive == active) return true;
    asset.proxyActive = active;
    assetUpdated(index);
    emit proxyChanged(asset.resourceId);
    return true;
}

QString MediaCatalog::proxyUriFor(const Asset &asset) const
{
    if (!asset.proxyActive || asset.proxyStatus != QLatin1String("ready")) return {};
    const QVariantMap entry = resource(asset.proxyResourceId);
    if (entry.isEmpty() || entry.value(QStringLiteral("availability")).toString() == QLatin1String("missing"))
        return {};
    const QString path = mediaLocalPath(entry.value(QStringLiteral("uri")).toString());
    return QFileInfo::exists(path) ? path : QString();
}

QString MediaCatalog::activeProxyUri(const QString &resourceId) const
{
    for (const Asset &asset : m_assets)
        if (asset.resourceId == resourceId) return proxyUriFor(asset);
    return {};
}

QVariantMap MediaCatalog::asset(const QString &assetId) const
{
    const int index = assetIndex(assetId);
    return index >= 0 ? assetMap(m_assets.at(index)) : QVariantMap();
}

QString MediaCatalog::assetIdForResource(const QString &resourceId) const
{
    for (const Asset &asset : m_assets)
        if (asset.resourceId == resourceId) return asset.assetId;
    return {};
}

QVariantMap MediaCatalog::contentMap() const
{
    QVariantList binList, assetList;
    for (const Bin &bin : m_bins)
        binList.append(QVariantMap{{QStringLiteral("binId"), bin.binId}, {QStringLiteral("name"), bin.name}});
    for (const Asset &asset : m_assets)
        assetList.append(QVariantMap{{QStringLiteral("assetId"), asset.assetId},
                                     {QStringLiteral("resourceId"), asset.resourceId},
                                     {QStringLiteral("label"), asset.label}, {QStringLiteral("binId"), asset.binId},
                                     {QStringLiteral("tags"), asset.tags},
                                     {QStringLiteral("proxyResourceId"), asset.proxyResourceId},
                                     {QStringLiteral("proxyActive"), asset.proxyActive}});
    return {{QStringLiteral("schema"), kSchema}, {QStringLiteral("bins"), binList},
            {QStringLiteral("assets"), assetList}};
}

bool MediaCatalog::loadContent(const QVariantMap &content)
{
    clearError();
    if (content.value(QStringLiteral("schema")).toString() != kSchema)
        return fail(tr("Expected a %1 catalog").arg(kSchema));
    QVector<Bin> bins;
    QSet<QString> binIds;
    for (const QVariant &value : content.value(QStringLiteral("bins")).toList()) {
        const QVariantMap map = value.toMap();
        const Bin bin{map.value(QStringLiteral("binId")).toString(), map.value(QStringLiteral("name")).toString()};
        if (bin.binId.isEmpty() || bin.name.trimmed().isEmpty() || binIds.contains(bin.binId) || bins.size() >= kMaxBins)
            return fail(tr("Catalog bins need unique IDs and names"));
        binIds.insert(bin.binId);
        bins.append(bin);
    }
    QVector<Asset> assets;
    QSet<QString> assetIds;
    for (const QVariant &value : content.value(QStringLiteral("assets")).toList()) {
        const QVariantMap map = value.toMap();
        Asset asset;
        asset.assetId = map.value(QStringLiteral("assetId")).toString();
        asset.resourceId = map.value(QStringLiteral("resourceId")).toString();
        asset.label = map.value(QStringLiteral("label")).toString();
        asset.binId = map.value(QStringLiteral("binId")).toString();
        asset.tags = map.value(QStringLiteral("tags")).toStringList();
        asset.proxyResourceId = map.value(QStringLiteral("proxyResourceId")).toString();
        asset.proxyStatus = asset.proxyResourceId.isEmpty() ? QStringLiteral("none") : QStringLiteral("ready");
        asset.proxyActive = !asset.proxyResourceId.isEmpty() && map.value(QStringLiteral("proxyActive")).toBool();
        if (asset.assetId.isEmpty() || asset.resourceId.isEmpty() || assetIds.contains(asset.assetId)
            || (!asset.binId.isEmpty() && !binIds.contains(asset.binId)) || assets.size() >= kMaxAssets)
            return fail(tr("Catalog assets need unique IDs, resource IDs and known bins"));
        if (asset.label.isEmpty()) asset.label = asset.resourceId;
        refreshEntry(asset);
        assetIds.insert(asset.assetId);
        assets.append(asset);
    }
    beginResetModel();
    m_bins = bins;
    m_assets = assets;
    m_visible.clear();
    if (!m_binFilter.isEmpty() && !binIds.contains(m_binFilter)) m_binFilter.clear();
    for (int i = 0; i < m_assets.size(); ++i)
        if (matches(m_assets.at(i))) m_visible.append(i);
    endResetModel();
    emit filterChanged();
    emit binsChanged();
    emit countChanged();
    for (int i = 0; i < m_assets.size(); ++i) probe(i);
    return true;
}

} // namespace QindaTK
