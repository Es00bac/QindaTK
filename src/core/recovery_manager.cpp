// SPDX-License-Identifier: LGPL-3.0-or-later
#include "recovery_manager.h"
#include <QDir>

namespace QindaTK {

RecoveryManager::RecoveryManager(QObject *parent) : QObject(parent) {}

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

void RecoveryManager::setCodecRegistry(std::shared_ptr<DocumentCodecRegistry> registry)
{
    if (registry) m_codecs = std::move(registry);
}

RecoveryJournal RecoveryManager::journal() const
{
    return RecoveryJournal({m_directory, m_maxEntries, m_maxJournalBytes, m_codecs}, m_shared);
}

void RecoveryManager::reportWrite(const OperationError &error)
{
    // A capture discarded by a save, or overtaken by a newer frame, is not a fault.
    if (error.code == ErrorCode::Cancelled || error.code == ErrorCode::StaleRevision) return;
    if (m_lastError == error.message) return;
    m_lastError = error.message;
    emit changed();
}

OperationError RecoveryManager::record(SnapshotPtr snapshot)
{
    const OperationError error = journal().append(snapshot);
    m_lastError = error.message;
    emit changed();
    return error;
}

std::optional<RecoverySnapshot> RecoveryManager::inspect(DocumentId id) const
{
    return journal().inspect(id);
}

std::optional<DecodedReferenceDocument> RecoveryManager::restore(DocumentId id,
                                                                  OperationError *error) const
{
    if (error) *error = {};
    const auto record = inspect(id);
    if (!record) {
        if (error) *error = {ErrorCode::Unavailable, QStringLiteral("No valid recovery revision"), {}, {}};
        return std::nullopt;
    }
    if (record->snapshot->header.schemaId != QString::fromLatin1(PropertyDocument::SchemaId)) {
        if (error) *error = {ErrorCode::Unsupported,
                             QStringLiteral("Recovery holds another document kind"), {}, {}};
        return std::nullopt;
    }
    return ReferenceDocumentCodec::decode(record->snapshot->payload, error);
}

std::optional<RecoveryManager::Restored> RecoveryManager::restoreDocument(DocumentId id,
                                                                          OperationError *error) const
{
    if (error) *error = {};
    const auto fail = [error](const OperationError &problem) -> std::optional<Restored> {
        if (error) *error = problem;
        return std::nullopt;
    };
    const auto record = inspect(id);
    if (!record)
        return fail({ErrorCode::Unavailable, QStringLiteral("No valid recovery revision"), {}, {}});
    const SnapshotHeader &header = record->snapshot->header;
    const DocumentCodecPtr codec = m_codecs->codec(header.schemaId);
    if (!codec)
        return fail({ErrorCode::Unavailable,
                     QStringLiteral("No codec is registered for recovery schema %1").arg(header.schemaId),
                     QStringLiteral("schemaId"), {}});
    // AGENT-GUARD: a decode failure (bounds, references, hostile content)
    // reports and returns; the recovery files stay for a capable build.
    OperationError problem;
    const DecodedDocumentPtr decoded = codec->decode(record->snapshot->payload, &problem);
    if (!decoded) return fail(problem);
    if (const auto mismatch = compareSnapshotHeaders(decoded->header(), header)) return fail(mismatch);
    return Restored{decoded, codec, record->snapshot};
}

bool RecoveryManager::discard(DocumentId id)
{
    const OperationError error = journal().discard(id);
    m_lastError = error.message;
    emit changed();
    return !error;
}

QVariantMap RecoveryManager::inspectId(const QString &documentId) const
{
    const auto result = inspect(DocumentId::parse(documentId));
    if (!result) return {};
    const SnapshotHeader &header = result->snapshot->header;
    return {{QStringLiteral("documentId"), documentId},
            {QStringLiteral("schemaId"), header.schemaId},
            {QStringLiteral("revision"), header.revision.toDecimal()},
            {QStringLiteral("resetGeneration"), QString::number(header.resetGeneration)},
            {QStringLiteral("truncatedTail"), result->truncatedTail},
            {QStringLiteral("codecAvailable"), result->codecAvailable},
            {QStringLiteral("validEntries"), result->validEntries}};
}

bool RecoveryManager::discardId(const QString &documentId)
{
    return discard(DocumentId::parse(documentId));
}

} // namespace QindaTK
