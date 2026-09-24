// SPDX-License-Identifier: LGPL-3.0-or-later
#include "recovery_journal.h"
#include "document_store.h"
#include "reference_document_codec.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace QindaTK {

struct RecoveryJournal::Shared {
    QMutex mutex;
    QHash<QString, quint64> epochs;
};

namespace {
constexpr quint32 frameMagic = 0x51544b52; // QTKR
constexpr qsizetype legacyHeaderBytes = 56;
constexpr qsizetype prefixBytes = 12;      // magic, version, header length
constexpr qsizetype digestBytes = 32;
constexpr qsizetype fixedHeaderBytes = 2 + 16 + 8 + 8 + 8 + digestBytes;
constexpr int maximumSchemaBytes = 255;

OperationError failure(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, QStringLiteral("recovery")};
}

QByteArray sha256(QByteArrayView bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QByteArray frameFor(const ImmutableSnapshot &snapshot)
{
    const QByteArray schema = snapshot.header.schemaId.toUtf8();
    QByteArray header;
    QDataStream out(&header, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << frameMagic << quint32(2) << quint32(fixedHeaderBytes + schema.size())
        << quint16(schema.size());
    out.writeRawData(schema.constData(), schema.size());
    const QByteArray uuid = snapshot.header.documentId.value.toRfc4122();
    out.writeRawData(uuid.constData(), uuid.size());
    out << quint64(snapshot.header.revision.value) << quint64(snapshot.header.resetGeneration)
        << quint64(snapshot.payload.size());
    header += sha256(snapshot.payload);
    return header + sha256(header) + snapshot.payload;
}

struct Frame {
    SnapshotHeader header;
    QByteArray payload;
    qsizetype size = 0;
};

// A complete, self-consistent frame at `offset`, or nothing.
std::optional<Frame> readFrame(const QByteArray &bytes, qsizetype offset, qsizetype maximumPayload)
{
    const qsizetype available = bytes.size() - offset;
    if (available < prefixBytes) return std::nullopt;
    QDataStream in(QByteArray::fromRawData(bytes.constData() + offset, available));
    in.setByteOrder(QDataStream::BigEndian);
    quint32 magic = 0, version = 0;
    in >> magic >> version;
    if (in.status() != QDataStream::Ok || magic != frameMagic) return std::nullopt;
    if (version == 1) {
        // Legacy reference frame: magic, version, length, revision, hash.
        quint64 length = 0, revision = 0;
        in >> length >> revision;
        if (in.status() != QDataStream::Ok || available < legacyHeaderBytes || length == 0
            || length > quint64(ReferenceDocumentCodec::MaximumBytes)
            || length > quint64(available - legacyHeaderBytes))
            return std::nullopt;
        const QByteArray payload = bytes.mid(offset + legacyHeaderBytes, qsizetype(length));
        if (sha256(payload) != bytes.mid(offset + 24, digestBytes)) return std::nullopt;
        const auto decoded = ReferenceDocumentCodec::decode(payload);
        if (!decoded || decoded->document->revision().value != revision) return std::nullopt;
        return Frame{decoded->document->header(), payload, legacyHeaderBytes + qsizetype(length)};
    }
    quint32 headerLength = 0;
    in >> headerLength;
    if (in.status() != QDataStream::Ok || version != 2 || headerLength < fixedHeaderBytes
        || headerLength > fixedHeaderBytes + maximumSchemaBytes
        || available < prefixBytes + qsizetype(headerLength) + digestBytes)
        return std::nullopt;
    const qsizetype headerEnd = prefixBytes + qsizetype(headerLength);
    if (sha256(QByteArrayView(bytes.constData() + offset, headerEnd))
        != bytes.mid(offset + headerEnd, digestBytes))
        return std::nullopt;
    quint16 schemaLength = 0;
    in >> schemaLength;
    if (schemaLength == 0 || fixedHeaderBytes + schemaLength != headerLength) return std::nullopt;
    QByteArray schema(schemaLength, Qt::Uninitialized);
    char uuid[16] = {};
    quint64 revision = 0, generation = 0, length = 0;
    if (in.readRawData(schema.data(), schemaLength) != schemaLength || in.readRawData(uuid, 16) != 16)
        return std::nullopt;
    in >> revision >> generation >> length;
    const qsizetype payloadOffset = headerEnd + digestBytes;
    if (in.status() != QDataStream::Ok || length == 0 || length > quint64(maximumPayload)
        || length > quint64(available - payloadOffset))
        return std::nullopt;
    const QByteArray payload = bytes.mid(offset + payloadOffset, qsizetype(length));
    if (sha256(payload) != bytes.mid(offset + headerEnd - digestBytes, digestBytes)) return std::nullopt;
    const DocumentId id{QUuid::fromRfc4122(QByteArrayView(uuid, 16))};
    if (!id.isValid()) return std::nullopt;
    return Frame{{QString::fromUtf8(schema), id, {revision}, generation}, payload,
                 payloadOffset + qsizetype(length)};
}

struct Scan {
    SnapshotPtr latest;
    bool truncated = false;
    bool codecAvailable = true;
    int entries = 0;
};

Scan scan(const QByteArray &bytes, DocumentId expected, const DocumentCodecRegistry &codecs,
          qsizetype maximumPayload)
{
    Scan result;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const auto frame = readFrame(bytes, offset, maximumPayload);
        const SnapshotPtr &latest = result.latest;
        // AGENT-GUARD: a frame is accepted only as a whole: both hashes, the
        // expected document, one schema per journal and no revision regress.
        if (!frame || frame->header.documentId != expected
            || (latest && (frame->header.schemaId != latest->header.schemaId
                           || frame->header.revision.value < latest->header.revision.value))) {
            result.truncated = true;
            break;
        }
        const DocumentCodecPtr codec = codecs.codec(frame->header.schemaId);
        if (codec) {
            const auto inspected = codec->inspect(frame->payload);
            if (!inspected || compareSnapshotHeaders(*inspected, frame->header)) {
                result.truncated = true;
                break;
            }
        }
        result.codecAvailable = bool(codec);
        result.latest = std::make_shared<const ImmutableSnapshot>(
            ImmutableSnapshot{frame->header, frame->payload});
        ++result.entries;
        offset += frame->size;
    }
    return result;
}

QByteArray readBounded(const QString &path, qsizetype maximum, bool *valid, bool *exists)
{
    QFile file(path);
    *exists = file.exists();
    if (!*exists) { *valid = true; return {}; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximum) {
        *valid = false;
        return {};
    }
    const QByteArray bytes = file.readAll();
    *valid = bytes.size() == file.size() && file.error() == QFileDevice::NoError;
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

OperationError writeAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return {ErrorCode::BackendFailure, file.errorString(), path, QStringLiteral("recovery")};
    return {};
}
} // namespace

std::shared_ptr<RecoveryJournal::Shared> RecoveryJournal::createShared()
{
    return std::make_shared<Shared>();
}

RecoveryJournal::RecoveryJournal(Options options, std::shared_ptr<Shared> shared)
    : m_options(std::move(options)), m_shared(std::move(shared))
{
    if (!m_options.codecs) m_options.codecs = DocumentCodecRegistry::shared();
}

bool RecoveryJournal::isValid() const
{
    return m_shared && !m_options.directory.isEmpty();
}

QString RecoveryJournal::path(DocumentId id, const QString &suffix) const
{
    return QDir(m_options.directory).filePath(id.toString() + suffix);
}

qsizetype RecoveryJournal::maximumFrameBytes() const
{
    return std::max(m_options.codecs->maximumEncodedBytes(), ReferenceDocumentCodec::MaximumBytes);
}

quint64 RecoveryJournal::epoch(DocumentId id) const
{
    if (!m_shared) return 0;
    QMutexLocker lock(&m_shared->mutex);
    return m_shared->epochs.value(id.toString());
}

std::optional<RecoverySnapshot> RecoveryJournal::inspect(DocumentId id) const
{
    if (!id.isValid() || !isValid()) return std::nullopt;
    QMutexLocker lock(&m_shared->mutex);
    return inspectLocked(id);
}

std::optional<RecoverySnapshot> RecoveryJournal::inspectLocked(DocumentId id) const
{
    const qsizetype frameBound = maximumFrameBytes() + prefixBytes + fixedHeaderBytes
        + maximumSchemaBytes + digestBytes;
    bool checkpointValid = false, journalValid = false, checkpointExists = false, journalExists = false;
    const QByteArray checkpointBytes = readBounded(path(id, QStringLiteral(".checkpoint")), frameBound,
                                                   &checkpointValid, &checkpointExists);
    const QByteArray journalBytes = readBounded(path(id, QStringLiteral(".journal")),
                                                m_options.maxJournalBytes + frameBound,
                                                &journalValid, &journalExists);
    const Scan base = scan(checkpointBytes, id, *m_options.codecs, maximumFrameBytes());
    const Scan journal = scan(journalBytes, id, *m_options.codecs, maximumFrameBytes());
    const bool truncated = !checkpointValid || !journalValid || base.truncated || journal.truncated
        || base.entries > 1;
    SnapshotPtr latest = base.latest;
    bool codecAvailable = base.codecAvailable;
    if (journal.latest && (!latest || (journal.latest->header.schemaId == latest->header.schemaId
                                       && journal.latest->header.revision.value
                                              >= latest->header.revision.value))) {
        latest = journal.latest;
        codecAvailable = journal.codecAvailable;
    }
    if (!latest) return std::nullopt;
    return RecoverySnapshot{latest, truncated, base.entries + journal.entries, codecAvailable};
}

OperationError RecoveryJournal::checkpoint(DocumentId id, const SnapshotPtr &snapshot) const
{
    // AGENT-GUARD: commit a validated checkpoint before clearing the journal.
    // If the process stops between those writes, inspect still has one copy.
    const auto error = writeAtomic(path(id, QStringLiteral(".checkpoint")), frameFor(*snapshot));
    if (error) return error;
    return writeAtomic(path(id, QStringLiteral(".journal")), {});
}

OperationError RecoveryJournal::setAsideCorrupt(DocumentId id) const
{
    // Nothing valid remains, but the bytes may still matter to a person:
    // keep them beside the journal instead of deleting them or appending
    // frames that a scan could never reach past the damage.
    for (const QString &suffix : {QStringLiteral(".checkpoint"), QStringLiteral(".journal")}) {
        const QString source = path(id, suffix);
        if (!QFile::exists(source)) continue;
        const QString aside = source + QStringLiteral(".corrupt");
        if (QFile::exists(aside) && !QFile::remove(aside))
            return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot replace old corrupt recovery copy"), aside);
        if (!QFile::rename(source, aside))
            return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot set corrupt recovery data aside"), source);
    }
    return {};
}

OperationError RecoveryJournal::append(const SnapshotPtr &snapshot,
                                       std::optional<quint64> expectedEpoch) const
{
    if (!snapshot || !snapshot->header.documentId.isValid() || !isValid())
        return failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid recovery request"));
    const DocumentCodecPtr codec = m_options.codecs->codec(snapshot->header.schemaId);
    if (!codec)
        return failure(ErrorCode::Unsupported, QStringLiteral("No codec is registered for this recovery schema"),
                       QStringLiteral("schemaId"));
    if (snapshot->header.schemaId.toUtf8().size() > maximumSchemaBytes || snapshot->payload.isEmpty()
        || snapshot->payload.size() > codec->maximumEncodedBytes())
        return failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid recovery snapshot"));
    const auto inspected = codec->inspect(snapshot->payload);
    if (!inspected || compareSnapshotHeaders(*inspected, snapshot->header))
        return failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid recovery snapshot"));
    const DocumentId id = snapshot->header.documentId;
    QMutexLocker lock(&m_shared->mutex);
    if (expectedEpoch && m_shared->epochs.value(id.toString()) != *expectedEpoch)
        return failure(ErrorCode::Cancelled, QStringLiteral("Recovery data was discarded after this capture"));
    if (!QDir().mkpath(m_options.directory))
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot create recovery directory"),
                       m_options.directory);
    const auto current = inspectLocked(id);
    if (current) {
        if (current->snapshot->header.schemaId != snapshot->header.schemaId)
            return failure(ErrorCode::Conflict, QStringLiteral("Recovery holds another schema for this document"));
        if (snapshot->header.revision.value < current->snapshot->header.revision.value)
            return failure(ErrorCode::StaleRevision, QStringLiteral("Older recovery revision"));
        if (current->truncatedTail) {
            if (const auto error = checkpoint(id, current->snapshot)) return error;
        }
    } else if (const auto error = setAsideCorrupt(id)) {
        return error;
    }
    const QByteArray frame = frameFor(*snapshot);
    QFile journal(path(id, QStringLiteral(".journal")));
    if (!journal.open(QIODevice::WriteOnly | QIODevice::Append) || journal.write(frame) != frame.size()
        || !durableFlush(journal))
        return {ErrorCode::BackendFailure, journal.errorString(), journal.fileName(), QStringLiteral("recovery")};
    if (journal.size() > m_options.maxJournalBytes
        || (current && current->validEntries + 1 >= m_options.maxEntries)) {
        journal.close();
        if (const auto error = checkpoint(id, snapshot)) return error;
    }
    return {};
}

OperationError RecoveryJournal::discard(DocumentId id) const
{
    if (!id.isValid() || !isValid())
        return failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid recovery request"));
    QMutexLocker lock(&m_shared->mutex);
    ++m_shared->epochs[id.toString()];
    bool ok = true;
    for (const QString &suffix : {QStringLiteral(".checkpoint"), QStringLiteral(".journal")}) {
        QFile file(path(id, suffix));
        if (file.exists() && !file.remove()) ok = false;
    }
    return ok ? OperationError{}
              : failure(ErrorCode::BackendFailure, QStringLiteral("Could not discard recovery data"));
}

} // namespace QindaTK
