// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_codec.h"
#include <memory>
#include <optional>

namespace QindaTK {

struct RecoverySnapshot {
    SnapshotPtr snapshot;
    bool truncatedTail = false;
    int validEntries = 0;
    // False when no registered codec can check or decode the offered frame;
    // it is still hash-validated and kept, but cannot be restored here.
    bool codecAvailable = true;
};

// AGENT-CONTRACT (docs/document-models.md#recovery-frames): a value handle on one
// recovery directory that any thread may use. RecoveryManager hands copies
// to save and autosave workers; all copies share one lock and one discard
// epoch per document, so frames never interleave and an append captured
// before a discard is refused. Frames written here are version 2 (schema,
// document UUID, revision, reset generation, payload length and hash, then a
// header hash); version-1 reference frames stay readable.
class RecoveryJournal {
public:
    struct Shared;
    struct Options {
        QString directory;
        int maxEntries = 32;
        qsizetype maxJournalBytes = qsizetype(64) * 1024 * 1024;
        std::shared_ptr<DocumentCodecRegistry> codecs;
    };
    static std::shared_ptr<Shared> createShared();
    RecoveryJournal() = default;
    RecoveryJournal(Options options, std::shared_ptr<Shared> shared);
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] quint64 epoch(DocumentId id) const;
    // expectedEpoch: the epoch read when the snapshot was captured.
    OperationError append(const SnapshotPtr &snapshot,
                          std::optional<quint64> expectedEpoch = std::nullopt) const;
    [[nodiscard]] std::optional<RecoverySnapshot> inspect(DocumentId id) const;
    OperationError discard(DocumentId id) const;

private:
    std::optional<RecoverySnapshot> inspectLocked(DocumentId id) const;
    OperationError checkpoint(DocumentId id, const SnapshotPtr &snapshot) const;
    OperationError setAsideCorrupt(DocumentId id) const;
    QString path(DocumentId id, const QString &suffix) const;
    qsizetype maximumFrameBytes() const;
    Options m_options;
    std::shared_ptr<Shared> m_shared;
};

} // namespace QindaTK
