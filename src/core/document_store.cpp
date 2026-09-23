// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_store.h"
#include "reference_document_codec.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace QindaTK {
namespace {
OperationError ioError(const QString &message, const QString &path)
{
    return {ErrorCode::BackendFailure, message, path, QStringLiteral("file")};
}

OperationError checkExpected(const SaveRequest &request)
{
    const QFileInfo info(request.path);
    if (request.expectation == FileExpectation::Missing) {
        if (info.exists() || info.isSymLink())
            return {ErrorCode::Conflict, QStringLiteral("Destination already exists"),
                    request.path, QStringLiteral("file")};
        return {};
    }
    if (request.expectation == FileExpectation::Any) return {};
    QFile file(request.path);
    if (!file.open(QIODevice::ReadOnly))
        return {ErrorCode::Conflict, QStringLiteral("File changed or disappeared"),
                request.path, QStringLiteral("file")};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result() != request.expectedSha256)
        return {ErrorCode::Conflict, QStringLiteral("File changed outside the application"),
                request.path, QStringLiteral("file")};
    return {};
}
} // namespace

QByteArray AtomicDocumentStore::sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

LoadReceipt AtomicDocumentStore::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, {}, ioError(file.errorString(), path)};
    if (file.size() < 1 || file.size() > ReferenceDocumentCodec::MaximumBytes)
        return {{}, {}, {ErrorCode::InvalidArgument,
                         QStringLiteral("Invalid reference document size"), path, {}}};
    const QByteArray bytes = file.readAll();
    if (bytes.size() != file.size() || file.error() != QFileDevice::NoError)
        return {{}, {}, ioError(file.errorString(), path)};
    return {bytes, sha256(bytes), {}};
}

SaveReceipt AtomicDocumentStore::save(const SaveRequest &request)
{
    // AGENT-CONTRACT: DocumentSession supplies immutable bytes and a baseline;
    // this worker adapter never accesses its live PropertyDocument QObject.
    SaveReceipt receipt;
    receipt.path = request.path;
    if (request.snapshot) receipt.saved = request.snapshot->header;
    if (!request.snapshot || !request.snapshot->header.documentId.isValid()
        || request.snapshot->payload.isEmpty()
        || request.snapshot->payload.size() > ReferenceDocumentCodec::MaximumBytes
        || request.path.isEmpty()
        || (request.expectation == FileExpectation::MatchingHash
            && request.expectedSha256.size() != 32)) {
        receipt.error = {ErrorCode::InvalidArgument, QStringLiteral("Invalid save request"),
                         request.path, {}};
        return receipt;
    }
    OperationError codecError;
    auto decoded = ReferenceDocumentCodec::decode(request.snapshot->payload, &codecError);
    if (!decoded || decoded->document->documentId() != receipt.saved.documentId
        || decoded->document->revision() != receipt.saved.revision
        || decoded->document->resetGeneration() != receipt.saved.resetGeneration) {
        receipt.error = {ErrorCode::InvalidArgument,
                         QStringLiteral("Snapshot header or payload is invalid"),
                         request.path, {}};
        return receipt;
    }
    if (request.cancellation.isCancelled()) {
        receipt.error = {ErrorCode::Cancelled, QStringLiteral("Save cancelled"), {}, {}};
        return receipt;
    }
    if (const auto conflict = checkExpected(request)) {
        receipt.error = conflict;
        return receipt;
    }
    QSaveFile file(request.path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        receipt.error = ioError(file.errorString(), request.path);
        return receipt;
    }
    if (file.write(request.snapshot->payload) != request.snapshot->payload.size()) {
        receipt.error = ioError(file.errorString(), request.path);
        file.cancelWriting();
        return receipt;
    }
    if (request.cancellation.isCancelled()) {
        file.cancelWriting();
        receipt.error = {ErrorCode::Cancelled, QStringLiteral("Save cancelled"), {}, {}};
        return receipt;
    }
    if (const auto conflict = checkExpected(request)) {
        file.cancelWriting();
        receipt.error = conflict;
        return receipt;
    }
    if (!file.commit()) {
        receipt.error = ioError(file.errorString(), request.path);
        return receipt;
    }
    receipt.sha256 = sha256(request.snapshot->payload);
    return receipt;
}

} // namespace QindaTK
