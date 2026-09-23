// SPDX-License-Identifier: LGPL-3.0-or-later
#include "resource_resolver.h"
#include "job_model.h"

#include <QSet>
#include <memory>

namespace QindaTK {
namespace {
constexpr qsizetype MaxProposalIds = 256;
constexpr qsizetype MaxCandidateUris = 2048;
}

QString ResourceResolver::refreshAsync(JobModel *jobs, const QString &resourceId)
{
    clearError();
    const auto it = m_entries.constFind(resourceId);
    if (!jobs || it == m_entries.cend() || it->origin != Origin::External) {
        fail(ErrorCode::InvalidArgument,
             QStringLiteral("An external resource and JobModel are required"));
        return {};
    }
    const QString uri = it->uri;
    const quint64 revision = it->revision;
    JobSpec spec;
    spec.label = QStringLiteral("Refresh resource %1").arg(resourceId);
    spec.work = [uri](const CancellationToken &cancel, const JobProgress &progress) {
        QByteArray hash;
        bool exists = false;
        const OperationError error = inspectExternal(uri, &hash, &exists, &cancel);
        if (progress) progress(1.0);
        return JobResult{{{QStringLiteral("sha256"), hash},
                          {QStringLiteral("exists"), exists}}, error};
    };
    const QString jobId = jobs->enqueue(std::move(spec));
    if (jobId.isEmpty()) {
        fail(ErrorCode::Unavailable, jobs->lastError());
        return {};
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(jobs, &JobModel::jobFinished, this,
        [this, connection, jobId, resourceId, uri, revision](
            const QString &finishedId, JobModel::JobState state,
            const QVariantMap &output, const QString &) {
        if (finishedId != jobId) return;
        disconnect(*connection);
        if (state != JobModel::Succeeded) return;
        auto current = m_entries.find(resourceId);
        if (current == m_entries.end() || current->uri != uri
            || current->revision != revision) return;
        const bool exists = output.value(QStringLiteral("exists")).toBool();
        const QByteArray hash = output.value(QStringLiteral("sha256")).toByteArray();
        if (!exists) current->availability = Availability::Missing;
        else {
            if (current->currentHash != hash) {
                current->currentHash = hash;
                ++current->revision;
            }
            if (current->expectedHash.isEmpty()) current->expectedHash = hash;
            current->availability = current->expectedHash == hash
                ? Availability::Ready : Availability::Modified;
        }
        emit resourceChanged(resourceId);
        emit changed();
    });
    return jobId;
}

QString ResourceResolver::proposeRelinksAsync(JobModel *jobs,
                                               const QStringList &resourceIds,
                                               const QStringList &candidateUris)
{
    clearError();
    if (!jobs || resourceIds.size() > MaxProposalIds
        || candidateUris.size() > MaxCandidateUris) {
        fail(ErrorCode::InvalidArgument,
             QStringLiteral("Relink search requires a JobModel and bounded inputs"));
        return {};
    }
    QVariantList targets;
    for (const QString &id : resourceIds) {
        const auto it = m_entries.constFind(id);
        if (it == m_entries.cend()) {
            fail(ErrorCode::Unavailable, QStringLiteral("Unknown resource ID"),
                 QStringLiteral("resourceIds"));
            return {};
        }
        targets.append(QVariantMap{{QStringLiteral("resourceId"), id},
            {QStringLiteral("currentUri"), it->uri},
            {QStringLiteral("hash"), it->expectedHash.isEmpty()
                ? it->currentHash : it->expectedHash}});
    }
    JobSpec spec;
    spec.label = QStringLiteral("Find resource relinks");
    spec.work = [targets, candidateUris](const CancellationToken &cancel,
                                          const JobProgress &progress) {
        QHash<QString, QByteArray> hashes;
        for (int index = 0; index < candidateUris.size(); ++index) {
            if (cancel.isCancelled())
                return JobResult{{}, {ErrorCode::Cancelled,
                    QStringLiteral("Relink search cancelled"), {}, {}}};
            const QString &uri = candidateUris.at(index);
            if (!hashes.contains(uri)) {
                QByteArray hash;
                bool exists = false;
                const OperationError error = inspectExternal(uri, &hash, &exists, &cancel);
                if (error) return JobResult{{}, error};
                if (exists) hashes.insert(uri, hash);
            }
            if (progress && !candidateUris.isEmpty())
                progress(double(index + 1) / candidateUris.size());
        }
        QVariantList proposals;
        for (const QVariant &targetValue : targets) {
            const QVariantMap target = targetValue.toMap();
            const QByteArray expected = target.value(QStringLiteral("hash")).toByteArray();
            if (expected.isEmpty()) continue;
            QSet<QString> emitted;
            for (const QString &uri : candidateUris) {
                if (emitted.contains(uri)) continue;
                emitted.insert(uri);
                if (hashes.value(uri) == expected)
                    proposals.append(QVariantMap{
                        {QStringLiteral("resourceId"), target.value(QStringLiteral("resourceId"))},
                        {QStringLiteral("currentUri"), target.value(QStringLiteral("currentUri"))},
                        {QStringLiteral("proposedUri"), uri},
                        {QStringLiteral("sha256"), QString::fromLatin1(expected)}});
            }
        }
        return JobResult{{{QStringLiteral("proposals"), proposals}}, {}};
    };
    const QString jobId = jobs->enqueue(std::move(spec));
    if (jobId.isEmpty()) fail(ErrorCode::Unavailable, jobs->lastError());
    return jobId;
}

} // namespace QindaTK
