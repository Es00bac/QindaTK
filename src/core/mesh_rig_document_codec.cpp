// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_codec.h"
#include "mesh_rig_document.h"
#include "undo_service.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaTK {
namespace {
constexpr qsizetype MaximumSessionBytes = 17 * 1024 * 1024;
OperationError meshError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("meshrig.codec")};
}
class MeshCapture final : public DocumentCapture {
public:
    MeshCapture(SnapshotHeader header, QByteArray bytes) : m_header(std::move(header)), m_bytes(std::move(bytes)) {}
    SnapshotHeader header() const override { return m_header; }
    QByteArray encode(OperationError *error) const override { if (error) *error = {}; return m_bytes; }
private:
    SnapshotHeader m_header;
    QByteArray m_bytes;
};
class MeshDecoded final : public DecodedDocument {
public:
    MeshDecoded(SnapshotHeader header, QByteArray bytes, QJsonObject extensions)
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

// AGENT-CONTRACT (D-157, docs/mesh-rig.md#sessions): a session file is the
// native version 2 mesh JSON plus a "session" object {documentId, revision,
// resetGeneration, extensions}. Plain saveToFile files (version 1 or 2, no
// session object) open too, with an identity derived from their bytes, so
// DocumentSession save, autosave and crash recovery cover mesh, rig and
// texture edits exactly like SceneModel3D.
class MeshRigDocumentCodec final : public DocumentCodec {
public:
    QString schemaId() const override { return QStringLiteral("qindatk.mesh-rig.v2"); }
    QString kind() const override { return QStringLiteral("meshrig"); }
    QString formatId() const override { return QStringLiteral("QindaTK.MeshRig"); }
    QString fileSuffix() const override { return QStringLiteral("qtkmesh"); }
    qsizetype maximumEncodedBytes() const override { return MaximumSessionBytes; }
    bool recognizes(const QByteArray &prefix) const override {
        return prefix.trimmed().startsWith('{') && prefix.contains("qindatk.mesh-rig");
    }
    static bool read(const QByteArray &bytes, MeshRigDocument::State *state, SnapshotHeader *header,
                     QJsonObject *extensions, QString *problem) {
        MeshRigDocument::State parsed;
        if (!MeshRigDocument::parseState(bytes, &parsed, problem)) return false;
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        SnapshotHeader result;
        result.schemaId = QStringLiteral("qindatk.mesh-rig.v2");
        QJsonObject ext;
        if (root.contains("session")) {
            const QJsonObject session = root.value("session").toObject();
            bool revisionOk = false, generationOk = false;
            result.documentId = DocumentId::parse(session.value("documentId").toString());
            result.revision.value = session.value("revision").toString().toULongLong(&revisionOk);
            result.resetGeneration = session.value("resetGeneration").toString().toULongLong(&generationOk);
            if (!result.documentId.isValid() || !revisionOk || !result.revision.value || !generationOk
                || !session.value("extensions").isObject()) {
                *problem = QStringLiteral("Invalid mesh session header");
                return false;
            }
            ext = session.value("extensions").toObject();
        } else {
            // Native files carry no identity; derive a stable one from the bytes.
            QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).left(16);
            hash[6] = char((quint8(hash[6]) & 0x0f) | 0x50);
            hash[8] = char((quint8(hash[8]) & 0x3f) | 0x80);
            result.documentId = {QUuid::fromRfc4122(hash)};
            result.revision.value = 1;
        }
        if (state) *state = parsed;
        if (header) *header = result;
        if (extensions) *extensions = ext;
        return true;
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override {
        SnapshotHeader header; QString problem;
        if (!read(bytes, nullptr, &header, nullptr, &problem)) {
            if (error) *error = meshError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return header;
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override {
        SnapshotHeader header; QJsonObject extensions; QString problem;
        if (!read(bytes, nullptr, &header, &extensions, &problem)) {
            if (error) *error = meshError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const MeshDecoded>(header, bytes, extensions);
    }
    std::unique_ptr<DocumentModel> create() const override { return std::make_unique<MeshRigDocument>(); }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded, OperationError *error) const override {
        const auto *source = dynamic_cast<const MeshDecoded *>(&decoded);
        if (!source) {
            if (error) *error = meshError(ErrorCode::InvalidArgument, QStringLiteral("Not a mesh document"));
            return {};
        }
        MeshRigDocument::State state; SnapshotHeader header; QString problem;
        if (!read(source->bytes(), &state, &header, nullptr, &problem)) {
            if (error) *error = meshError(ErrorCode::Validation, problem);
            return {};
        }
        auto mesh = std::make_unique<MeshRigDocument>();
        mesh->m_localHistory->removeModel(mesh.get());
        mesh->m_documentId = header.documentId.toString();
        mesh->m_revision = header.revision.value;
        mesh->m_resetGeneration = header.resetGeneration;
        mesh->m_state = std::move(state);
        mesh->m_localHistory->addModel(mesh.get());
        mesh->m_localHistory->setStepLimit(mesh.get(), 16);
        mesh->m_localHistory->markModelClean(mesh.get());
        if (error) *error = {};
        return mesh;
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override {
        const auto *mesh = dynamic_cast<const MeshRigDocument *>(&model);
        if (!mesh) {
            if (error) *error = meshError(ErrorCode::InvalidArgument, QStringLiteral("Not a mesh document"));
            return {};
        }
        if (QJsonDocument(extensions).toJson(QJsonDocument::Compact).size() > 64 * 1024) {
            if (error) *error = meshError(ErrorCode::Unsupported, QStringLiteral("Mesh extensions exceed 64 KiB"));
            return {};
        }
        // toJson() records committed state: an open paint stroke is excluded.
        QJsonObject root = QJsonDocument::fromJson(mesh->toJson().toUtf8()).object();
        const SnapshotHeader header = mesh->modelHeader();
        root.insert("session", QJsonObject{{"documentId", header.documentId.toString()},
                                           {"revision", QString::number(header.revision.value)},
                                           {"resetGeneration", QString::number(header.resetGeneration)},
                                           {"extensions", extensions}});
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (bytes.size() > maximumEncodedBytes()) {
            if (error) *error = meshError(ErrorCode::Unsupported, QStringLiteral("Mesh document exceeds its size limit"));
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const MeshCapture>(header, bytes);
    }
};

DocumentCodecPtr meshRigDocumentCodec()
{
    static const DocumentCodecPtr codec = std::make_shared<const MeshRigDocumentCodec>();
    return codec;
}
} // namespace QindaTK
