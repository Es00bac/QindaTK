// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_codec.h"
#include "scene_model_3d.h"
#include "undo_service.h"

#include <QJsonDocument>

namespace QindaTK {
namespace {
OperationError sceneError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("scene3d.codec")};
}
class SceneCapture final : public DocumentCapture {
public:
    SceneCapture(SnapshotHeader header, QByteArray bytes)
        : m_header(std::move(header)), m_bytes(std::move(bytes)) {}
    SnapshotHeader header() const override { return m_header; }
    QByteArray encode(OperationError *error) const override {
        if (error) *error = {};
        return m_bytes;
    }
private:
    SnapshotHeader m_header;
    QByteArray m_bytes;
};
class SceneDecoded final : public DecodedDocument {
public:
    SceneDecoded(SnapshotHeader header, QByteArray bytes, QJsonObject extensions)
        : m_header(std::move(header)), m_bytes(std::move(bytes)), m_extensions(std::move(extensions)) {}
    SnapshotHeader header() const override { return m_header; }
    QJsonObject extensions() const override { return m_extensions; }
    const QByteArray &bytes() const { return m_bytes; }
private:
    SnapshotHeader m_header;
    QByteArray m_bytes;
    QJsonObject m_extensions;
};
} // namespace

class SceneDocumentCodec final : public DocumentCodec {
public:
    QString schemaId() const override { return QStringLiteral("qindatk.scene3d.v1"); }
    QString kind() const override { return QStringLiteral("scene3d"); }
    QString formatId() const override { return QStringLiteral("QindaTK.Scene3D"); }
    QString fileSuffix() const override { return QStringLiteral("qtkscene"); }
    qsizetype maximumEncodedBytes() const override { return 4 * 1024 * 1024; }
    bool recognizes(const QByteArray &prefix) const override {
        return prefix.trimmed().startsWith('{') && prefix.contains("QindaTK.Scene3D");
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override {
        SnapshotHeader header;
        QString problem;
        if (!SceneModel3D::decodeNative(bytes, nullptr, &header, nullptr, &problem)) {
            if (error) *error = sceneError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return header;
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override {
        SnapshotHeader header;
        QJsonObject extensions;
        QString problem;
        if (!SceneModel3D::decodeNative(bytes, nullptr, &header, &extensions, &problem)) {
            if (error) *error = sceneError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const SceneDecoded>(header, bytes, extensions);
    }
    std::unique_ptr<DocumentModel> create() const override { return std::make_unique<SceneModel3D>(); }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded, OperationError *error) const override {
        const auto *source = dynamic_cast<const SceneDecoded *>(&decoded);
        if (!source) {
            if (error) *error = sceneError(ErrorCode::InvalidArgument, QStringLiteral("Not a scene document"));
            return {};
        }
        QVector<SceneModel3D::Entity> entities;
        SnapshotHeader header;
        QString problem;
        if (!SceneModel3D::decodeNative(source->bytes(), &entities, &header, nullptr, &problem)) {
            if (error) *error = sceneError(ErrorCode::Validation, problem);
            return {};
        }
        auto scene = std::make_unique<SceneModel3D>();
        scene->m_localHistory->removeModel(scene.get());
        scene->m_documentId = header.documentId.toString();
        scene->m_revision = header.revision.value;
        scene->m_resetGeneration = header.resetGeneration;
        scene->m_entities = std::move(entities);
        scene->m_localHistory->addModel(scene.get());
        scene->m_localHistory->setStepLimit(scene.get(), 64);
        scene->m_localHistory->markModelClean(scene.get());
        if (error) *error = {};
        return scene;
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override {
        const auto *scene = dynamic_cast<const SceneModel3D *>(&model);
        if (!scene) {
            if (error) *error = sceneError(ErrorCode::InvalidArgument, QStringLiteral("Not a scene model"));
            return {};
        }
        if (QJsonDocument(extensions).toJson(QJsonDocument::Compact).size() > 64 * 1024) {
            if (error) *error = sceneError(ErrorCode::Unsupported, QStringLiteral("Scene extensions exceed 64 KiB"));
            return {};
        }
        QJsonObject root = QJsonDocument::fromJson(scene->toJson().toUtf8()).object();
        const SnapshotHeader header = scene->modelHeader();
        root["version"] = 3;
        root["documentId"] = header.documentId.toString();
        root["revision"] = QString::number(header.revision.value);
        root["resetGeneration"] = QString::number(header.resetGeneration);
        root["extensions"] = extensions;
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (bytes.size() > maximumEncodedBytes()) {
            if (error) *error = sceneError(ErrorCode::Unsupported, QStringLiteral("Scene exceeds 4 MiB"));
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const SceneCapture>(header, bytes);
    }
};

DocumentCodecPtr sceneDocumentCodec()
{
    static const DocumentCodecPtr codec = std::make_shared<const SceneDocumentCodec>();
    return codec;
}
} // namespace QindaTK
