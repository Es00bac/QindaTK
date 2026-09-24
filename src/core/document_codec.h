// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_model.h"
#include <QJsonObject>
#include <QReadWriteLock>
#include <QVariantMap>
#include <QVector>
#include <memory>
#include <optional>

namespace QindaTK {

// Immutable capture taken on the GUI thread. It shares, never references,
// live model state, so encode() can run on a worker while editing goes on.
class DocumentCapture {
public:
    virtual ~DocumentCapture() = default;
    [[nodiscard]] virtual SnapshotHeader header() const = 0;
    [[nodiscard]] virtual QByteArray encode(OperationError *error) const = 0;
};
using DocumentCapturePtr = std::shared_ptr<const DocumentCapture>;

// Validated, bounded result of DocumentCodec::decode: plain data that can
// cross threads. Only DocumentCodec::adopt turns it into a live model.
class DecodedDocument {
public:
    virtual ~DecodedDocument() = default;
    [[nodiscard]] virtual SnapshotHeader header() const = 0;
    [[nodiscard]] virtual QJsonObject extensions() const = 0;
};
using DecodedDocumentPtr = std::shared_ptr<const DecodedDocument>;

// AGENT-CONTRACT (docs/document-models.md#codecs): the persistence half of a
// domain. schemaId() equals the modelHeader().schemaId of every model the
// codec creates, captures or adopts; it is the key recovery frames, save
// receipts and workspace records carry. inspect/decode/encode are reentrant
// and touch no QObject; create/adopt/capture run on the GUI thread. decode
// validates identity, references and resource bounds before it allocates
// decoded resources, so adopt never sees an invalid document.
class DocumentCodec {
public:
    virtual ~DocumentCodec() = default;
    [[nodiscard]] virtual QString schemaId() const = 0;
    [[nodiscard]] virtual QString kind() const = 0;
    [[nodiscard]] virtual QString formatId() const = 0;
    [[nodiscard]] virtual QString fileSuffix() const = 0;
    [[nodiscard]] virtual qsizetype maximumEncodedBytes() const = 0;
    // Content signature check on a file prefix; never the extension.
    [[nodiscard]] virtual bool recognizes(const QByteArray &prefix) const = 0;
    [[nodiscard]] virtual std::optional<SnapshotHeader> inspect(
        const QByteArray &bytes, OperationError *error = nullptr) const = 0;
    [[nodiscard]] virtual DecodedDocumentPtr decode(const QByteArray &bytes,
                                                    OperationError *error = nullptr) const = 0;
    [[nodiscard]] virtual std::unique_ptr<DocumentModel> create() const = 0;
    [[nodiscard]] virtual std::unique_ptr<DocumentModel> adopt(
        const DecodedDocument &decoded, OperationError *error = nullptr) const = 0;
    [[nodiscard]] virtual DocumentCapturePtr capture(const DocumentModel &model,
                                                     const QJsonObject &extensions,
                                                     OperationError *error = nullptr) const = 0;
    [[nodiscard]] QVariantMap info() const;
};
using DocumentCodecPtr = std::shared_ptr<const DocumentCodec>;

// Thread-safe schema -> codec map. shared() is the process-wide registry with
// the built-in reference and canvas codecs; an application registers its own
// domain codecs there at startup, or gives a DocumentManager an isolated one.
class DocumentCodecRegistry {
public:
    static std::shared_ptr<DocumentCodecRegistry> shared();
    static std::shared_ptr<DocumentCodecRegistry> withBuiltins();
    bool registerCodec(DocumentCodecPtr codec);
    [[nodiscard]] DocumentCodecPtr codec(const QString &schemaId) const;
    [[nodiscard]] DocumentCodecPtr detect(const QByteArray &prefix) const;
    [[nodiscard]] QVector<DocumentCodecPtr> codecs() const;
    [[nodiscard]] qsizetype maximumEncodedBytes() const;

private:
    mutable QReadWriteLock m_lock;
    QVector<DocumentCodecPtr> m_codecs;
};

// Codecs for the built-in model families.
DocumentCodecPtr referenceDocumentCodec();
DocumentCodecPtr canvasDocumentCodec();
DocumentCodecPtr workbookDocumentCodec(); // qindatk.workbook.v1 (docs/workbook.md#sessions)

// Empty result when `actual` names the same schema, document, revision and
// reset generation as `expected`; otherwise the first mismatch.
OperationError compareSnapshotHeaders(const SnapshotHeader &actual,
                                      const SnapshotHeader &expected);
// Encodes a capture into the immutable snapshot a store or journal receives,
// enforcing the codec's encoded bound and the header/payload identity.
SnapshotPtr encodeCapture(const DocumentCodec &codec, const DocumentCapture &capture,
                          OperationError *error = nullptr);

} // namespace QindaTK
