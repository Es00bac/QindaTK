// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document.h"
#include "canvas_document_format.h"

#include <QJsonDocument>

namespace QindaTK {
namespace {
OperationError failure(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("canvas.codec")};
}

class CanvasCapture final : public DocumentCapture {
public:
    explicit CanvasCapture(CanvasDocumentState state) : m_state(std::move(state)) {}
    SnapshotHeader header() const override
    {
        return {QString::fromLatin1(CanvasDocumentFormat::SchemaId), m_state.documentId,
                {m_state.revision}, m_state.resetGeneration};
    }
    QByteArray encode(OperationError *error) const override
    {
        return CanvasDocumentFormat::encode(m_state, error);
    }
private:
    const CanvasDocumentState m_state;
};

class CanvasDecoded final : public DecodedDocument {
public:
    explicit CanvasDecoded(CanvasDocumentState state) : state(std::move(state)) {}
    SnapshotHeader header() const override
    {
        return {QString::fromLatin1(CanvasDocumentFormat::SchemaId), state.documentId,
                {state.revision}, state.resetGeneration};
    }
    QJsonObject extensions() const override { return state.extensions; }
    const CanvasDocumentState state;
};

class CanvasCodec final : public DocumentCodec {
public:
    explicit CanvasCodec(const CanvasDocumentLimits &limits) : m_limits(limits) {}
    QString schemaId() const override { return QString::fromLatin1(CanvasDocumentFormat::SchemaId); }
    QString kind() const override { return QStringLiteral("canvas"); }
    QString formatId() const override { return QString::fromLatin1(CanvasDocumentFormat::FormatId); }
    QString fileSuffix() const override { return QStringLiteral("qtkc"); }
    qsizetype maximumEncodedBytes() const override { return m_limits.maximumEncodedBytes; }
    bool recognizes(const QByteArray &prefix) const override
    {
        return CanvasDocumentFormat::recognizes(prefix);
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override
    {
        return CanvasDocumentFormat::inspect(bytes, error);
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override
    {
        auto state = CanvasDocumentFormat::decode(bytes, m_limits, error);
        return state ? std::make_shared<const CanvasDecoded>(std::move(*state)) : DecodedDocumentPtr{};
    }
    std::unique_ptr<DocumentModel> create() const override
    {
        auto document = std::make_unique<CanvasDocument>();
        document->setLimits(m_limits);
        return document;
    }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded, OperationError *error) const override
    {
        const auto *canvas = dynamic_cast<const CanvasDecoded *>(&decoded);
        if (!canvas) {
            if (error) *error = failure(ErrorCode::InvalidArgument, QStringLiteral("Decoded document belongs to another codec"));
            return {};
        }
        if (error) *error = {};
        return CanvasDocument::fromState(canvas->state, m_limits);
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override
    {
        const auto *document = dynamic_cast<const CanvasDocument *>(&model);
        if (!document) {
            if (error) *error = failure(ErrorCode::InvalidArgument, QStringLiteral("Model is not a canvas document"));
            return {};
        }
        CanvasDocumentState state = document->state();
        state.extensions = extensions;
        // AGENT-GUARD: guides share the saved extensions block, so the limit
        // applies to host extensions and guides together (decode enforces it).
        if (QJsonDocument(CanvasDocumentFormat::extensionsWithGuides(state)).toJson(QJsonDocument::Compact).size()
            > m_limits.maximumExtensionBytes) {
            if (error) *error = failure(ErrorCode::Unsupported, QStringLiteral("Canvas extensions exceed the size limit"));
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const CanvasCapture>(std::move(state));
    }
private:
    const CanvasDocumentLimits m_limits;
};
} // namespace

DocumentCodecPtr makeCanvasDocumentCodec(const CanvasDocumentLimits &limits)
{
    return std::make_shared<const CanvasCodec>(limits);
}

DocumentCodecPtr canvasDocumentCodec()
{
    static const DocumentCodecPtr codec = makeCanvasDocumentCodec({});
    return codec;
}

} // namespace QindaTK
