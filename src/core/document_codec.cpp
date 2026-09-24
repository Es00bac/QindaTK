// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_codec.h"
#include "reference_document_codec.h"
#include <QReadLocker>
#include <QWriteLocker>

namespace QindaTK {
namespace {
constexpr auto referenceFormatId = "qindatk.reference-property-document";

OperationError invalid(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, QStringLiteral("document.codec")};
}

class ReferenceCapture final : public DocumentCapture {
public:
    explicit ReferenceCapture(SnapshotPtr snapshot) : m_snapshot(std::move(snapshot)) {}
    SnapshotHeader header() const override { return m_snapshot->header; }
    QByteArray encode(OperationError *error) const override
    {
        if (error) *error = {};
        return m_snapshot->payload;
    }
private:
    SnapshotPtr m_snapshot;
};

class ReferenceDecoded final : public DecodedDocument {
public:
    ReferenceDecoded(QByteArray bytes, SnapshotHeader header, QJsonObject extensions)
        : bytes(std::move(bytes)), m_header(std::move(header)), m_extensions(std::move(extensions)) {}
    SnapshotHeader header() const override { return m_header; }
    QJsonObject extensions() const override { return m_extensions; }
    const QByteArray bytes;
private:
    SnapshotHeader m_header;
    QJsonObject m_extensions;
};

// AGENT-NOTE: adapts the version-2 reference JSON codec to the generic
// boundary without changing its schema or file bytes. The JSON form has no
// cheap header, so inspect() is a full validating decode; reference
// documents are small by design (ReferenceDocumentCodec::MaximumBytes).
class ReferenceCodec final : public DocumentCodec {
public:
    QString schemaId() const override { return QString::fromLatin1(PropertyDocument::SchemaId); }
    QString kind() const override { return QStringLiteral("property"); }
    QString formatId() const override { return QString::fromLatin1(referenceFormatId); }
    QString fileSuffix() const override { return QStringLiteral("qtk"); }
    qsizetype maximumEncodedBytes() const override { return ReferenceDocumentCodec::MaximumBytes; }
    bool recognizes(const QByteArray &prefix) const override
    {
        const QByteArray trimmed = prefix.trimmed();
        return trimmed.startsWith('{') && trimmed.contains(referenceFormatId);
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override
    {
        auto decoded = ReferenceDocumentCodec::decode(bytes, error);
        if (!decoded) return std::nullopt;
        return decoded->document->header();
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override
    {
        auto decoded = ReferenceDocumentCodec::decode(bytes, error);
        if (!decoded) return {};
        return std::make_shared<const ReferenceDecoded>(bytes, decoded->document->header(),
                                                        decoded->extensions);
    }
    std::unique_ptr<DocumentModel> create() const override
    {
        return std::make_unique<PropertyDocument>();
    }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded,
                                         OperationError *error) const override
    {
        const auto *reference = dynamic_cast<const ReferenceDecoded *>(&decoded);
        if (!reference) {
            if (error) *error = invalid(ErrorCode::InvalidArgument, QStringLiteral("Decoded document belongs to another codec"));
            return {};
        }
        // Decode again on the adopting (GUI) thread so the QObject is born there.
        auto document = ReferenceDocumentCodec::decode(reference->bytes, error);
        if (!document) return {};
        if (const auto mismatch = compareSnapshotHeaders(document->document->header(), reference->header())) {
            if (error) *error = mismatch;
            return {};
        }
        return std::move(document->document);
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override
    {
        const auto *document = dynamic_cast<const PropertyDocument *>(&model);
        if (!document) {
            if (error) *error = invalid(ErrorCode::InvalidArgument, QStringLiteral("Model is not a reference property document"));
            return {};
        }
        const SnapshotPtr snapshot = ReferenceDocumentCodec::capture(*document, extensions, error);
        return snapshot ? std::make_shared<const ReferenceCapture>(snapshot) : DocumentCapturePtr{};
    }
};
} // namespace

QVariantMap DocumentCodec::info() const
{
    return {{QStringLiteral("schemaId"), schemaId()},
            {QStringLiteral("kind"), kind()},
            {QStringLiteral("formatId"), formatId()},
            {QStringLiteral("fileSuffix"), fileSuffix()},
            {QStringLiteral("maximumEncodedBytes"), QString::number(maximumEncodedBytes())}};
}

DocumentCodecPtr referenceDocumentCodec()
{
    static const DocumentCodecPtr codec = std::make_shared<const ReferenceCodec>();
    return codec;
}

std::shared_ptr<DocumentCodecRegistry> DocumentCodecRegistry::shared()
{
    static const std::shared_ptr<DocumentCodecRegistry> registry = withBuiltins();
    return registry;
}

std::shared_ptr<DocumentCodecRegistry> DocumentCodecRegistry::withBuiltins()
{
    auto registry = std::make_shared<DocumentCodecRegistry>();
    registry->registerCodec(referenceDocumentCodec());
    registry->registerCodec(canvasDocumentCodec());
    registry->registerCodec(workbookDocumentCodec());
    return registry;
}

bool DocumentCodecRegistry::registerCodec(DocumentCodecPtr codec)
{
    if (!codec || codec->schemaId().isEmpty() || codec->maximumEncodedBytes() < 1) return false;
    QWriteLocker lock(&m_lock);
    for (const auto &existing : std::as_const(m_codecs))
        if (existing->schemaId() == codec->schemaId()) return false;
    m_codecs.append(std::move(codec));
    return true;
}

DocumentCodecPtr DocumentCodecRegistry::codec(const QString &schemaId) const
{
    QReadLocker lock(&m_lock);
    for (const auto &entry : m_codecs)
        if (entry->schemaId() == schemaId) return entry;
    return {};
}

DocumentCodecPtr DocumentCodecRegistry::detect(const QByteArray &prefix) const
{
    QReadLocker lock(&m_lock);
    for (const auto &entry : m_codecs)
        if (entry->recognizes(prefix)) return entry;
    return {};
}

QVector<DocumentCodecPtr> DocumentCodecRegistry::codecs() const
{
    QReadLocker lock(&m_lock);
    return m_codecs;
}

qsizetype DocumentCodecRegistry::maximumEncodedBytes() const
{
    QReadLocker lock(&m_lock);
    qsizetype maximum = 0;
    for (const auto &entry : m_codecs) maximum = std::max(maximum, entry->maximumEncodedBytes());
    return maximum;
}

OperationError compareSnapshotHeaders(const SnapshotHeader &actual, const SnapshotHeader &expected)
{
    if (actual.schemaId != expected.schemaId)
        return invalid(ErrorCode::InvalidArgument, QStringLiteral("Snapshot schema does not match"),
                       QStringLiteral("schemaId"));
    if (!actual.documentId.isValid() || actual.documentId != expected.documentId)
        return invalid(ErrorCode::InvalidArgument, QStringLiteral("Snapshot document identity does not match"),
                       QStringLiteral("documentId"));
    if (actual.revision != expected.revision)
        return invalid(ErrorCode::StaleRevision, QStringLiteral("Snapshot revision does not match"),
                       QStringLiteral("revision"));
    if (actual.resetGeneration != expected.resetGeneration)
        return invalid(ErrorCode::StaleGeneration, QStringLiteral("Snapshot reset generation does not match"),
                       QStringLiteral("resetGeneration"));
    return {};
}

SnapshotPtr encodeCapture(const DocumentCodec &codec, const DocumentCapture &capture,
                          OperationError *error)
{
    if (error) *error = {};
    const SnapshotHeader header = capture.header();
    if (header.schemaId != codec.schemaId() || !header.documentId.isValid()) {
        if (error) *error = invalid(ErrorCode::InvalidArgument, QStringLiteral("Capture does not belong to this codec"));
        return {};
    }
    OperationError problem;
    QByteArray bytes = capture.encode(&problem);
    if (problem || bytes.isEmpty()) {
        if (error) *error = problem ? problem
            : invalid(ErrorCode::BackendFailure, QStringLiteral("Document encoding produced no bytes"));
        return {};
    }
    if (bytes.size() > codec.maximumEncodedBytes()) {
        if (error) *error = invalid(ErrorCode::Unsupported, QStringLiteral("Document exceeds the encoded size limit"));
        return {};
    }
    return std::make_shared<const ImmutableSnapshot>(ImmutableSnapshot{header, std::move(bytes)});
}

} // namespace QindaTK
