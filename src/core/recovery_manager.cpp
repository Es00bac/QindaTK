// SPDX-License-Identifier: LGPL-3.0-or-later
#include "recovery_manager.h"
#include "document_store.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace QindaTK {
namespace {
constexpr quint32 frameMagic = 0x51544b52; // QTKR
constexpr quint32 frameVersion = 1;
constexpr qsizetype frameHeaderBytes = 56;

QByteArray frameFor(const ImmutableSnapshot &snapshot)
{
    QByteArray bytes;
    bytes.reserve(frameHeaderBytes + snapshot.payload.size());
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << frameMagic << frameVersion
           << quint64(snapshot.payload.size()) << snapshot.header.revision.value;
    bytes.append(AtomicDocumentStore::sha256(snapshot.payload));
    bytes.append(snapshot.payload);
    return bytes;
}

bool durableFlush(QFile &file)
{
    if (!file.flush()) return false;
#ifdef Q_OS_UNIX
    if (::fsync(file.handle()) != 0) return false;
#endif
    return true;
}

struct Scan {
    SnapshotPtr latest;
    bool truncated = false;
    int entries = 0;
};

Scan scan(const QByteArray &bytes, DocumentId expected,
          const std::function<bool(const QByteArray &, SnapshotHeader *)> &readHeader)
{
    Scan result;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < frameHeaderBytes) { result.truncated = true; break; }
        QDataStream stream(bytes.mid(offset, 24));
        stream.setByteOrder(QDataStream::BigEndian);
        quint32 magic = 0, version = 0;
        quint64 length = 0, revision = 0;
        stream >> magic >> version >> length >> revision;
        if (stream.status() != QDataStream::Ok || magic != frameMagic
            || version != frameVersion || length == 0
            || length > quint64(ReferenceDocumentCodec::MaximumBytes)
            || length > quint64(bytes.size() - offset - frameHeaderBytes)) {
            result.truncated = true;
            break;
        }
        const QByteArray hash = bytes.mid(offset + 24, 32);
        const QByteArray payload = bytes.mid(offset + frameHeaderBytes, qsizetype(length));
        SnapshotHeader header;
        header.revision.value = revision; // frames carry the revision
        if (AtomicDocumentStore::sha256(payload) != hash || !readHeader(payload, &header)
            || header.documentId != expected || header.revision.value != revision
            || (result.latest && revision < result.latest->header.revision.value)) {
            result.truncated = true;
            break;
        }
        result.latest = std::make_shared<const ImmutableSnapshot>(ImmutableSnapshot{header, payload});
        ++result.entries;
        offset += frameHeaderBytes + qsizetype(length);
    }
    return result;
}

QByteArray readBounded(const QString &path, qsizetype maximum, bool *valid)
{
    QFile file(path);
    if (!file.exists()) { *valid = true; return {}; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximum) {
        *valid = false;
        return {};
    }
    const QByteArray bytes = file.readAll();
    *valid = bytes.size() == file.size() && file.error() == QFileDevice::NoError;
    return bytes;
}

OperationError writeAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit())
        return {ErrorCode::BackendFailure, file.errorString(), path,
                QStringLiteral("recovery")};
    return {};
}
} // namespace

RecoveryManager::RecoveryManager(QObject *parent) : QObject(parent) {}

void RecoveryManager::addPayloadValidator(PayloadValidator validator)
{
    if (validator) m_validators.push_back(std::move(validator));
}

bool RecoveryManager::readHeader(const QByteArray &payload, SnapshotHeader *header) const
{
    OperationError error;
    if (auto decoded = ReferenceDocumentCodec::decode(payload, &error)) {
        *header = decoded->document->header();
        return true;
    }
    for (const auto &validator : m_validators) {
        SnapshotHeader candidate = *header;
        if (!validator(payload, &candidate)) {
            *header = candidate;
            return true;
        }
    }
    return false;
}

void RecoveryManager::setDirectory(const QString &path)
{
    const QString absolute = path.isEmpty() ? QString{} : QDir(path).absolutePath();
    if (m_directory == absolute) return;
    m_directory = absolute;
    emit changed();
}

void RecoveryManager::setMaxJournalEntries(int count)
{
    if (count < 1 || count > 1024 || count == m_maxEntries) return;
    m_maxEntries = count;
    emit changed();
}

void RecoveryManager::setMaxJournalBytes(const QString &bytes)
{
    bool ok = false;
    const quint64 parsed = bytes.toULongLong(&ok, 10);
    if (!ok || QString::number(parsed) != bytes || parsed < 1024 * 1024
        || parsed > 1024ULL * 1024 * 1024
        || parsed == quint64(m_maxJournalBytes)) return;
    m_maxJournalBytes = qsizetype(parsed);
    emit changed();
}

QString RecoveryManager::path(DocumentId id, const QString &suffix) const
{
    return QDir(m_directory).filePath(id.toString() + suffix);
}

std::optional<RecoverySnapshot> RecoveryManager::inspect(DocumentId id) const
{
    if (!id.isValid() || m_directory.isEmpty()) return std::nullopt;
    bool checkpointReadable = false, journalReadable = false;
    const QByteArray checkpointBytes = readBounded(
        path(id, QStringLiteral(".checkpoint")),
        ReferenceDocumentCodec::MaximumBytes + frameHeaderBytes, &checkpointReadable);
    const QByteArray journalBytes = readBounded(
        path(id, QStringLiteral(".journal")),
        m_maxJournalBytes + ReferenceDocumentCodec::MaximumBytes + frameHeaderBytes,
        &journalReadable);
    const auto reader = [this](const QByteArray &payload, SnapshotHeader *header) {
        return readHeader(payload, header);
    };
    Scan base = scan(checkpointBytes, id, reader);
    Scan journal = scan(journalBytes, id, reader);
    const bool truncated = !checkpointReadable || !journalReadable || base.truncated
        || journal.truncated || base.entries > 1;
    SnapshotPtr latest = base.latest;
    if (journal.latest && (!latest || journal.latest->header.revision.value
                                       >= latest->header.revision.value))
        latest = journal.latest;
    if (!latest) return std::nullopt;
    return RecoverySnapshot{latest, truncated, base.entries + journal.entries};
}

std::optional<DecodedReferenceDocument> RecoveryManager::restore(
    DocumentId id, OperationError *error) const
{
    if (error) *error = {};
    const auto record = inspect(id);
    if (!record) {
        if (error) *error = {ErrorCode::Unavailable,
                             QStringLiteral("No valid recovery revision"), {}, {}};
        return std::nullopt;
    }
    return ReferenceDocumentCodec::decode(record->snapshot->payload, error);
}

OperationError RecoveryManager::checkpoint(DocumentId id, SnapshotPtr snapshot)
{
    // AGENT-GUARD: commit a validated checkpoint before clearing the journal.
    // If the process stops between those writes, inspect still has one copy.
    const auto error = writeAtomic(path(id, QStringLiteral(".checkpoint")),
                                   frameFor(*snapshot));
    if (error) return error;
    return writeAtomic(path(id, QStringLiteral(".journal")), {});
}

OperationError RecoveryManager::record(SnapshotPtr snapshot)
{
    const auto fail = [this](OperationError error) {
        m_lastError = error.message;
        emit changed();
        return error;
    };
    if (!snapshot || !snapshot->header.documentId.isValid() || m_directory.isEmpty())
        return fail({ErrorCode::InvalidArgument,
                     QStringLiteral("Invalid recovery request"), {}, {}});
    SnapshotHeader header = snapshot->header;
    if (!readHeader(snapshot->payload, &header) || header.documentId != snapshot->header.documentId
        || header.revision != snapshot->header.revision
        || header.resetGeneration != snapshot->header.resetGeneration)
        return fail({ErrorCode::InvalidArgument,
                     QStringLiteral("Invalid recovery snapshot"), {}, {}});
    if (!QDir().mkpath(m_directory))
        return fail({ErrorCode::BackendFailure,
                     QStringLiteral("Cannot create recovery directory"),
                     m_directory, QStringLiteral("recovery")});
    const DocumentId id = snapshot->header.documentId;
    const auto current = inspect(id);
    if (current && snapshot->header.revision.value
                       < current->snapshot->header.revision.value)
        return fail({ErrorCode::StaleRevision,
                     QStringLiteral("Older recovery revision"), {}, {}});
    if (current && current->truncatedTail) {
        const auto error = checkpoint(id, current->snapshot);
        if (error) return fail(error);
    }
    const QByteArray frame = frameFor(*snapshot);
    QFile journal(path(id, QStringLiteral(".journal")));
    if (!journal.open(QIODevice::WriteOnly | QIODevice::Append)
        || journal.write(frame) != frame.size() || !durableFlush(journal))
        return fail({ErrorCode::BackendFailure, journal.errorString(),
                     journal.fileName(), QStringLiteral("recovery")});
    if (journal.size() > m_maxJournalBytes
        || (current && current->validEntries + 1 >= m_maxEntries)) {
        journal.close();
        const auto error = checkpoint(id, snapshot);
        if (error) return fail(error);
    }
    m_lastError.clear();
    emit changed();
    return {};
}

bool RecoveryManager::discard(DocumentId id)
{
    if (!id.isValid() || m_directory.isEmpty()) return false;
    bool ok = true;
    for (const QString &suffix : {QStringLiteral(".checkpoint"),
                                  QStringLiteral(".journal")}) {
        QFile file(path(id, suffix));
        if (file.exists() && !file.remove()) ok = false;
    }
    if (!ok) m_lastError = QStringLiteral("Could not discard recovery data");
    else m_lastError.clear();
    emit changed();
    return ok;
}

QVariantMap RecoveryManager::inspectId(const QString &documentId) const
{
    const auto result = inspect(DocumentId::parse(documentId));
    if (!result) return {};
    return {{QStringLiteral("documentId"), documentId},
            {QStringLiteral("revision"), result->snapshot->header.revision.toDecimal()},
            {QStringLiteral("truncatedTail"), result->truncatedTail},
            {QStringLiteral("validEntries"), result->validEntries}};
}

bool RecoveryManager::discardId(const QString &documentId)
{
    return discard(DocumentId::parse(documentId));
}

} // namespace QindaTK
