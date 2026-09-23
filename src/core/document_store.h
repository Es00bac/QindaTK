// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <memory>

namespace QindaTK {

enum class FileExpectation { Missing, MatchingHash, Any };

struct SaveRequest {
    SnapshotPtr snapshot;
    QString path;
    FileExpectation expectation = FileExpectation::Missing;
    QByteArray expectedSha256;
    CancellationToken cancellation;
};

struct SaveReceipt {
    SnapshotHeader saved;
    QString path;
    QByteArray sha256;
    OperationError error;
    explicit operator bool() const { return !error; }
};

struct LoadReceipt {
    QByteArray bytes;
    QByteArray sha256;
    OperationError error;
    explicit operator bool() const { return !error; }
};

// Worker-side seam: tests inject failures/barriers; the production adapter
// uses QSaveFile and never falls back to direct writes.
class DocumentStore {
public:
    virtual ~DocumentStore() = default;
    virtual SaveReceipt save(const SaveRequest &request) = 0;
    virtual LoadReceipt load(const QString &path) = 0;
};

class AtomicDocumentStore final : public DocumentStore {
public:
    SaveReceipt save(const SaveRequest &request) override;
    LoadReceipt load(const QString &path) override;
    static QByteArray sha256(const QByteArray &bytes);
};

using DocumentStorePtr = std::shared_ptr<DocumentStore>;

} // namespace QindaTK
