// SPDX-License-Identifier: LGPL-3.0-or-later
#include "resource_resolver.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace QindaTK {
namespace {
constexpr qint64 HashChunkBytes = 1024 * 1024;
constexpr qsizetype MaxProposalIds = 256;
constexpr qsizetype MaxCandidateUris = 2048;
}

ResourceResolver::ResourceResolver(QObject *parent) : QObject(parent) {}

QVariantList ResourceResolver::resources() const
{
    QStringList keys = m_entries.keys();
    std::sort(keys.begin(), keys.end());
    QVariantList result;
    result.reserve(keys.size());
    for (const QString &key : keys)
        result.push_back(entryMap(m_entries.value(key)));
    return result;
}

bool ResourceResolver::validId(const QString &id)
{
    return !id.trimmed().isEmpty() && id.size() <= 256
        && !id.contains(QChar::Null);
}

bool ResourceResolver::validHash(const QByteArray &hash)
{
    if (hash.isEmpty())
        return true;
    if (hash.size() != 64)
        return false;
    for (const char c : hash) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
              || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

QString ResourceResolver::availabilityName(Availability availability)
{
    switch (availability) {
    case Availability::Missing: return QStringLiteral("missing");
    case Availability::Ready: return QStringLiteral("ready");
    case Availability::Modified: return QStringLiteral("modified");
    }
    return QStringLiteral("missing");
}

QString ResourceResolver::originName(Origin origin)
{
    return origin == Origin::Embedded ? QStringLiteral("embedded")
                                      : QStringLiteral("external");
}

bool ResourceResolver::localPath(const QString &uri, QString *path)
{
    const QUrl url(uri);
    if (!url.isValid())
        return false;
    if (url.scheme().isEmpty()) {
        *path = QFileInfo(uri).absoluteFilePath();
        return !path->isEmpty();
    }
    if (url.isLocalFile()) {
        *path = url.toLocalFile();
        return !path->isEmpty();
    }
    return false;
}

OperationError ResourceResolver::inspectExternal(const QString &uri,
                                                  QByteArray *hash,
                                                  bool *exists)
{
    QString path;
    if (!localPath(uri, &path))
        return {ErrorCode::Unsupported,
                QStringLiteral("External resources require a local path or file URI."),
                QStringLiteral("uri"), {}};
    const QFileInfo info(path);
    *exists = info.exists();
    if (!*exists)
        return {};
    if (!info.isFile() || !info.isReadable())
        return {ErrorCode::Unavailable,
                QStringLiteral("Resource is not a readable regular file."),
                QStringLiteral("uri"), {}};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {ErrorCode::Unavailable, file.errorString(), QStringLiteral("uri"), {}};

    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(HashChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            return {ErrorCode::BackendFailure, file.errorString(),
                    QStringLiteral("uri"), {}};
        digest.addData(chunk);
    }
    *hash = digest.result().toHex();
    return {};
}

QVariantMap ResourceResolver::entryMap(const Entry &entry) const
{
    return {{QStringLiteral("resourceId"), entry.id},
            {QStringLiteral("uri"), entry.uri},
            {QStringLiteral("revision"), QString::number(entry.revision)},
            {QStringLiteral("expectedSha256"), QString::fromLatin1(entry.expectedHash)},
            {QStringLiteral("sha256"), QString::fromLatin1(entry.currentHash)},
            {QStringLiteral("availability"), availabilityName(entry.availability)},
            {QStringLiteral("origin"), originName(entry.origin)}};
}

bool ResourceResolver::refreshEntry(Entry *entry, bool *didChange)
{
    if (entry->origin == Origin::Embedded) {
        entry->availability = Availability::Ready;
        if (entry->currentHash.isEmpty())
            entry->currentHash = entry->expectedHash;
        if (entry->expectedHash.isEmpty())
            entry->expectedHash = entry->currentHash;
        entry->availability = entry->expectedHash == entry->currentHash
            ? Availability::Ready : Availability::Modified;
        return true;
    }

    QByteArray observed;
    bool exists = false;
    const OperationError error = inspectExternal(entry->uri, &observed, &exists);
    if (error)
        return fail(error.code, error.message, error.fieldPath);
    if (!exists) {
        entry->availability = Availability::Missing;
        return true;
    }
    if (entry->currentHash != observed) {
        entry->currentHash = observed;
        ++entry->revision;
        if (didChange)
            *didChange = true;
    }
    if (entry->expectedHash.isEmpty())
        entry->expectedHash = observed;
    entry->availability = entry->expectedHash == observed
        ? Availability::Ready : Availability::Modified;
    return true;
}

bool ResourceResolver::fail(ErrorCode code, const QString &message,
                            const QString &field)
{
    m_error = {code, message, field, {}};
    emit changed();
    return false;
}

void ResourceResolver::clearError()
{
    if (m_error) {
        m_error = {};
        emit changed();
    }
}

bool ResourceResolver::registerResource(const QString &resourceId,
                                        const QString &uri,
                                        const QByteArray &expectedSha256,
                                        const QString &origin)
{
    clearError();
    if (!validId(resourceId) || uri.trimmed().isEmpty())
        return fail(ErrorCode::InvalidArgument,
                    QStringLiteral("A resource ID and URI are required."));
    if (m_entries.contains(resourceId))
        return fail(ErrorCode::Conflict,
                    QStringLiteral("The logical resource ID is already registered."),
                    QStringLiteral("resourceId"));
    if (!validHash(expectedSha256))
        return fail(ErrorCode::InvalidArgument,
                    QStringLiteral("SHA-256 must contain exactly 64 hexadecimal characters."),
                    QStringLiteral("expectedSha256"));

    Entry entry;
    entry.id = resourceId;
    entry.uri = uri;
    entry.expectedHash = expectedSha256.toLower();
    if (origin == QStringLiteral("embedded")) {
        entry.origin = Origin::Embedded;
        entry.currentHash = entry.expectedHash;
        entry.availability = Availability::Ready;
        entry.revision = entry.currentHash.isEmpty() ? 0 : 1;
    } else if (origin == QStringLiteral("external")) {
        entry.origin = Origin::External;
        bool changed = false;
        if (!refreshEntry(&entry, &changed))
            return false;
    } else {
        return fail(ErrorCode::InvalidArgument,
                    QStringLiteral("Origin must be 'external' or 'embedded'."),
                    QStringLiteral("origin"));
    }
    m_entries.insert(resourceId, entry);
    emit changed();
    emit resourceChanged(resourceId);
    return true;
}

QVariantMap ResourceResolver::resolve(const QString &resourceId)
{
    clearError();
    auto it = m_entries.find(resourceId);
    if (it == m_entries.end()) {
        fail(ErrorCode::Unavailable,
             QStringLiteral("The logical resource ID is not registered."),
             QStringLiteral("resourceId"));
        return {};
    }
    bool didChange = false;
    if (!refreshEntry(&it.value(), &didChange))
        return {};
    if (didChange)
        emit resourceChanged(resourceId);
    emit changed();
    return entryMap(it.value());
}

QVariantMap ResourceResolver::lookup(const QString &resourceId)
{
    clearError();
    const auto it = m_entries.constFind(resourceId);
    if (it == m_entries.cend()) {
        fail(ErrorCode::Unavailable,
             QStringLiteral("The logical resource ID is not registered."),
             QStringLiteral("resourceId"));
        return {};
    }
    return entryMap(it.value());
}

bool ResourceResolver::refresh(const QString &resourceId)
{
    clearError();
    auto it = m_entries.find(resourceId);
    if (it == m_entries.end())
        return fail(ErrorCode::Unavailable,
                    QStringLiteral("The logical resource ID is not registered."),
                    QStringLiteral("resourceId"));
    bool didChange = false;
    if (!refreshEntry(&it.value(), &didChange))
        return false;
    emit changed();
    emit resourceChanged(resourceId);
    return true;
}

bool ResourceResolver::relink(const QString &resourceId, const QString &newUri)
{
    clearError();
    auto it = m_entries.find(resourceId);
    if (it == m_entries.end())
        return fail(ErrorCode::Unavailable,
                    QStringLiteral("The logical resource ID is not registered."),
                    QStringLiteral("resourceId"));
    if (it->origin != Origin::External)
        return fail(ErrorCode::Unsupported,
                    QStringLiteral("Embedded resources cannot be relinked to external files."),
                    QStringLiteral("resourceId"));
    if (newUri.trimmed().isEmpty())
        return fail(ErrorCode::InvalidArgument,
                    QStringLiteral("A destination URI is required."),
                    QStringLiteral("newUri"));

    QByteArray observed;
    bool exists = false;
    const OperationError error = inspectExternal(newUri, &observed, &exists);
    if (error)
        return fail(error.code, error.message, error.fieldPath);
    if (!exists)
        return fail(ErrorCode::Unavailable,
                    QStringLiteral("The relink destination does not exist."),
                    QStringLiteral("newUri"));

    const QString oldUri = it->uri;
    it->uri = newUri;
    if (it->currentHash != observed) {
        it->currentHash = observed;
        ++it->revision;
    }
    if (it->expectedHash.isEmpty())
        it->expectedHash = observed;
    it->availability = it->expectedHash == observed
        ? Availability::Ready : Availability::Modified;
    emit changed();
    emit resourceChanged(resourceId);
    emit relinked(resourceId, oldUri, newUri);
    return true;
}

QVariantList ResourceResolver::proposeRelinks(const QStringList &resourceIds,
                                              const QStringList &candidateUris)
{
    clearError();
    if (resourceIds.size() > MaxProposalIds || candidateUris.size() > MaxCandidateUris)
        return fail(ErrorCode::InvalidArgument,
                    QStringLiteral("Relink search exceeds its bounded input size.")),
               QVariantList{};

    for (const QString &id : resourceIds) {
        if (!m_entries.contains(id))
            return fail(ErrorCode::Unavailable,
                        QStringLiteral("A requested logical resource ID is not registered."),
                        QStringLiteral("resourceIds")), QVariantList{};
    }

    QHash<QString, QByteArray> candidateHashes;
    for (const QString &uri : candidateUris) {
        if (candidateHashes.contains(uri))
            continue;
        QByteArray hash;
        bool exists = false;
        const OperationError error = inspectExternal(uri, &hash, &exists);
        if (error)
            return fail(error.code, error.message, error.fieldPath), QVariantList{};
        if (exists)
            candidateHashes.insert(uri, hash);
    }

    QVariantList proposals;
    for (const QString &id : resourceIds) {
        const auto it = m_entries.constFind(id);
        if (it == m_entries.cend()) {
            return fail(ErrorCode::Unavailable,
                        QStringLiteral("A requested logical resource ID is not registered."),
                        QStringLiteral("resourceIds")), QVariantList{};
        }
        const QByteArray target = it->expectedHash.isEmpty()
            ? it->currentHash : it->expectedHash;
        if (target.isEmpty())
            continue;
        QSet<QString> emittedCandidates;
        for (const QString &uri : candidateUris) {
            if (emittedCandidates.contains(uri))
                continue;
            emittedCandidates.insert(uri);
            if (candidateHashes.value(uri) != target)
                continue;
            QVariantMap proposal;
            proposal.insert(QStringLiteral("resourceId"), id);
            proposal.insert(QStringLiteral("currentUri"), it->uri);
            proposal.insert(QStringLiteral("proposedUri"), uri);
            proposal.insert(QStringLiteral("sha256"), QString::fromLatin1(target));
            proposals.push_back(proposal);
        }
    }
    return proposals;
}

bool ResourceResolver::removeResource(const QString &resourceId)
{
    clearError();
    if (m_entries.remove(resourceId) == 0)
        return fail(ErrorCode::Unavailable,
                    QStringLiteral("The logical resource ID is not registered."),
                    QStringLiteral("resourceId"));
    emit changed();
    emit resourceChanged(resourceId);
    return true;
}

} // namespace QindaTK
