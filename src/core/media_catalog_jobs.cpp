// SPDX-License-Identifier: LGPL-3.0-or-later
// MediaCatalog jobs: metadata probes, proxies, test signals and folder relink.
#include "media_catalog.h"
#include "media_decoder.h"
#include "media_export.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace QindaTK {
namespace {
QString fileUri(const QString &path)
{
    return QUrl::fromLocalFile(path).toString();
}

qint64 optionInteger(const QVariantMap &options, const char *name, qint64 fallback)
{
    const QVariant value = options.value(QString::fromLatin1(name));
    if (!value.isValid()) return fallback;
    bool ok = false;
    const qint64 parsed = value.metaType().id() == QMetaType::QString ? value.toString().toLongLong(&ok)
                                                                     : value.toLongLong(&ok);
    return ok ? parsed : -1;
}
} // namespace

void MediaCatalog::probe(int index)
{
    if (index < 0 || index >= m_assets.size()) return;
    Asset &asset = m_assets[index];
    refreshEntry(asset);
    const quint64 generation = ++asset.probeGeneration;
    const QString uri = asset.entry.value(QStringLiteral("uri")).toString();
    const QString path = mediaLocalPath(uri);
    if (asset.entry.isEmpty() || path.isEmpty()
        || asset.entry.value(QStringLiteral("availability")).toString() == QLatin1String("missing")
        || !QFileInfo::exists(path)) {
        asset.metadataState = QStringLiteral("missing");
        asset.error = tr("Media is offline");
        assetUpdated(index);
        return;
    }
    JobSpec spec;
    spec.label = tr("Read media: %1").arg(asset.label);
    spec.priority = 5;
    spec.work = [path](const CancellationToken &token, const JobProgress &) {
        JobResult result;
        MediaDecoder decoder;
        result.error = decoder.open(path, token);
        if (result.error)
            result.output = {{QStringLiteral("offline"), result.error.code == ErrorCode::Unavailable}};
        else
            result.output = decoder.info()->toVariantMap();
        return result;
    };
    const QString jobId = m_jobs->enqueue(std::move(spec));
    if (jobId.isEmpty()) {
        asset.metadataState = QStringLiteral("failed");
        asset.error = m_jobs->lastError();
        assetUpdated(index);
        return;
    }
    m_pending.insert(jobId, {QStringLiteral("metadata"), asset.assetId, generation, path});
    asset.metadataState = QStringLiteral("pending");
    asset.error.clear();
    assetUpdated(index);
}

void MediaCatalog::refresh(const QString &assetId)
{
    for (int i = 0; i < m_assets.size(); ++i) {
        if (!assetId.isEmpty() && m_assets.at(i).assetId != assetId) continue;
        // ResourceResolver::refresh re-inspects the file; its resourceChanged
        // signal re-reads metadata through onResourceChanged.
        if (m_resolver && m_resolver->refresh(m_assets.at(i).resourceId)) continue;
        probe(i);
    }
}

void MediaCatalog::onResourceChanged(const QString &resourceId)
{
    for (int i = 0; i < m_assets.size(); ++i) {
        Asset &asset = m_assets[i];
        if (asset.resourceId == resourceId) {
            probe(i);
        } else if (!asset.proxyResourceId.isEmpty() && asset.proxyResourceId == resourceId) {
            assetUpdated(i);
            emit proxyChanged(asset.resourceId);
        }
    }
}

QString MediaCatalog::createProxy(const QString &assetId, int maxHeight)
{
    clearError();
    const int index = assetIndex(assetId);
    if (index < 0) return fail(tr("Unknown asset %1").arg(assetId)), QString();
    Asset &asset = m_assets[index];
    refreshEntry(asset);
    const QString source = mediaLocalPath(asset.entry.value(QStringLiteral("uri")).toString());
    if (source.isEmpty() || !QFileInfo::exists(source)) return fail(tr("%1 is offline").arg(asset.label)), QString();
    if (asset.proxyStatus == QLatin1String("creating")) return fail(tr("A proxy for %1 is already being made").arg(asset.label)), QString();
    if (m_proxyDirectory.isEmpty() || !QDir().mkpath(m_proxyDirectory))
        return fail(tr("The proxy directory is not writable")), QString();
    QString stem = asset.assetId;
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    const QString target = QDir(m_proxyDirectory).filePath(
        QStringLiteral("%1-%2.proxy.mkv").arg(stem.left(80)).arg(++m_serial));
    JobSpec spec;
    spec.label = tr("Create proxy: %1").arg(asset.label);
    spec.priority = 1;
    spec.work = [source, target, maxHeight](const CancellationToken &token, const JobProgress &progress) {
        const MediaExportResult written = MediaExport::writeProxy(source, target, maxHeight, token, progress);
        JobResult result;
        result.error = written.error;
        result.committed = !written.error;
        result.output = {{QStringLiteral("path"), target}, {QStringLiteral("frames"), QString::number(written.frames)}};
        return result;
    };
    const QString jobId = m_jobs->enqueue(std::move(spec));
    if (jobId.isEmpty()) return fail(m_jobs->lastError()), QString();
    m_pending.insert(jobId, {QStringLiteral("proxy"), asset.assetId, 0, target});
    asset.proxyStatus = QStringLiteral("creating");
    asset.proxyError.clear();
    assetUpdated(index);
    return jobId;
}

QString MediaCatalog::generateTestSignal(const QString &uri, const QVariantMap &options)
{
    clearError();
    const QString path = mediaLocalPath(uri);
    if (path.isEmpty()) return fail(tr("A test signal needs a local file path")), QString();
    MediaTestSignalSettings settings;
    settings.frameSize = QSize(int(optionInteger(options, "width", 320)), int(optionInteger(options, "height", 180)));
    const auto rate = TimelineRate::normalized(optionInteger(options, "frameRateNumerator", 25),
                                               optionInteger(options, "frameRateDenominator", 1));
    if (!rate) return fail(tr("The test signal frame rate is invalid")), QString();
    settings.frameRate = *rate;
    settings.frames = optionInteger(options, "frames", 50);
    settings.sampleRate = int(optionInteger(options, "sampleRate", 48000));
    settings.channels = int(optionInteger(options, "channels", 2));
    settings.popInterval = optionInteger(options, "popInterval", (settings.frameRate.numerator
        + settings.frameRate.denominator / 2) / settings.frameRate.denominator);
    settings.popPhase = optionInteger(options, "popPhase", 0);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail(tr("Cannot create %1").arg(path)), QString();
    JobSpec spec;
    spec.label = tr("Write test signal: %1").arg(QFileInfo(path).fileName());
    spec.work = [path, settings](const CancellationToken &token, const JobProgress &progress) {
        const MediaExportResult written = MediaExport::writeTestSignal(path, settings, token, progress);
        JobResult result;
        result.error = written.error;
        result.committed = !written.error;
        result.output = {{QStringLiteral("path"), path}, {QStringLiteral("frames"), QString::number(written.frames)}};
        return result;
    };
    const QString jobId = m_jobs->enqueue(std::move(spec));
    if (jobId.isEmpty()) return fail(m_jobs->lastError()), QString();
    m_pending.insert(jobId, {QStringLiteral("testSignal"), {}, 0, path});
    return jobId;
}

void MediaCatalog::onJobFinished(const QString &jobId, JobModel::JobState state, const QVariantMap &result,
                                 const QString &error)
{
    const auto pending = m_pending.find(jobId);
    if (pending == m_pending.end()) return;
    const PendingJob job = pending.value();
    m_pending.erase(pending);
    const bool ok = state == JobModel::Succeeded;
    if (job.kind == QLatin1String("testSignal")) {
        emit jobFinished(jobId, job.kind, {}, ok, ok ? QString() : error);
        return;
    }
    const int index = assetIndex(job.assetId);
    if (index < 0) {
        emit jobFinished(jobId, job.kind, job.assetId, false, tr("The asset was removed"));
        return;
    }
    Asset &asset = m_assets[index];
    if (job.kind == QLatin1String("metadata")) {
        if (job.generation != asset.probeGeneration) return; // a newer probe owns the row
        if (ok) {
            asset.metadata = result;
            asset.metadataState = QStringLiteral("ready");
            asset.error.clear();
        } else {
            asset.metadata.clear();
            asset.metadataState = result.value(QStringLiteral("offline")).toBool() ? QStringLiteral("missing")
                                                                                  : QStringLiteral("failed");
            asset.error = error;
        }
        assetUpdated(index);
        emit jobFinished(jobId, job.kind, job.assetId, ok, ok ? QString() : error);
        return;
    }
    // Proxy: register the finished file as its own resource so relink,
    // availability and hashing work exactly as for originals.
    bool registered = false;
    QString proxyError = error;
    if (ok && m_resolver) {
        const QString proxyId = QStringLiteral("proxy:%1:%2").arg(asset.resourceId).arg(++m_serial);
        registered = m_resolver->registerResource(proxyId, fileUri(job.path));
        if (registered) {
            const QString previous = m_assets[index].proxyResourceId;
            m_assets[index].proxyResourceId = proxyId;
            if (!previous.isEmpty()) m_resolver->removeResource(previous);
        } else {
            proxyError = m_resolver->lastError();
        }
    } else if (ok) {
        proxyError = tr("A resource resolver is required to register the proxy");
    }
    Asset &updated = m_assets[index];
    updated.proxyStatus = registered ? QStringLiteral("ready")
        : state == JobModel::Cancelled ? QStringLiteral("none") : QStringLiteral("failed");
    updated.proxyError = registered ? QString() : proxyError;
    if (!registered) updated.proxyActive = false;
    assetUpdated(index);
    emit proxyChanged(updated.resourceId);
    emit jobFinished(jobId, job.kind, job.assetId, registered, registered ? QString() : proxyError);
}

int MediaCatalog::relinkMissingFrom(const QString &folderUri)
{
    clearError();
    const QDir folder(mediaLocalPath(folderUri));
    if (!m_resolver || folderUri.isEmpty() || !folder.exists()) return fail(tr("Choose an existing local folder")), 0;
    QHash<QString, QStringList> byName; // missing file name -> resource IDs
    for (Asset &asset : m_assets) {
        refreshEntry(asset);
        const QString path = mediaLocalPath(asset.entry.value(QStringLiteral("uri")).toString());
        if (path.isEmpty() || QFileInfo::exists(path)) continue;
        byName[QFileInfo(path).fileName()].append(asset.resourceId);
    }
    if (byName.isEmpty()) return 0;
    QStringList candidates;
    QStringList missing;
    const QFileInfoList files = folder.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &file : files) {
        if (candidates.size() >= kMaxRelinkCandidates) break;
        const auto it = byName.constFind(file.fileName());
        if (it == byName.cend()) continue;
        candidates.append(fileUri(file.absoluteFilePath()));
        missing.append(it.value());
    }
    missing.removeDuplicates();
    int relinked = 0;
    QSet<QString> done;
    // AGENT-NOTE: only exact content-hash matches relink (ResourceResolver::
    // proposeRelinks); a same-named file with other content stays unlinked.
    constexpr qsizetype kIdsPerCall = 256;
    constexpr qsizetype kCandidatesPerCall = 2048;
    for (qsizetype first = 0; first < missing.size(); first += kIdsPerCall) {
        const QStringList ids = missing.mid(first, kIdsPerCall);
        for (qsizetype offset = 0; offset < candidates.size(); offset += kCandidatesPerCall) {
            const QVariantList proposals = m_resolver->proposeRelinks(ids, candidates.mid(offset, kCandidatesPerCall));
            for (const QVariant &value : proposals) {
                const QVariantMap proposal = value.toMap();
                const QString id = proposal.value(QStringLiteral("resourceId")).toString();
                if (done.contains(id)) continue;
                if (m_resolver->relink(id, proposal.value(QStringLiteral("proposedUri")).toString())) {
                    done.insert(id);
                    ++relinked;
                }
            }
        }
    }
    return relinked;
}

} // namespace QindaTK
