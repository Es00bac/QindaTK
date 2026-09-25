// SPDX-License-Identifier: LGPL-3.0-or-later
// qindatk.local-data-design.v1: the form/report design file and its
// DocumentCodec (docs/forms-reports.md#design-document). The file is the
// design map itself plus a "session" member with the document identity, so
// LocalDataDesignStore reads the same file. Encoding is compact and
// canonical (QJsonObject sorts keys), so save -> reopen -> save is stable.
#include "document_codec.h"
#include "local_data_design_document.h"
#include "local_data_design_store.h"

#include <QJsonDocument>
#include <QUuid>

namespace QindaTK {
namespace {
const char *const Format = "qindatk.local-data-design";
// UUIDv5 namespace for identities of version 1 files, which carry none.
const QUuid VersionOneNamespace(QStringLiteral("{6f1d7c1e-5a0b-4f7e-9d6b-2f3c0a9e5b41}"));

OperationError codecError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("data-design.codec")};
}
} // namespace

QByteArray LocalDataDesignDocument::encodeState(const QJsonObject &design, const SnapshotHeader &header,
                                                const QJsonObject &extensions)
{
    QJsonObject root = design;
    root.insert(QStringLiteral("session"), QJsonObject{
        {QStringLiteral("documentId"), header.documentId.toString()},
        {QStringLiteral("revision"), header.revision.toDecimal()},
        {QStringLiteral("resetGeneration"), QString::number(header.resetGeneration)},
        {QStringLiteral("extensions"), extensions}});
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool LocalDataDesignDocument::decodeState(const QByteArray &bytes, QJsonObject *design, SnapshotHeader *header,
                                          QJsonObject *extensions, QString *problem)
{
    auto failWith = [problem](const QString &message) { if (problem) *problem = message; return false; };
    if (bytes.size() > MaximumBytes) return failWith(QStringLiteral("Design exceeds 4 MiB"));
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return failWith(QStringLiteral("Design is not JSON: %1").arg(parse.errorString()));
    QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(Format) || (version != 1 && version != 2))
        return failWith(QStringLiteral("Not a qindatk.local-data-design version 1 or 2 file"));
    const QJsonObject session = root.take(QStringLiteral("session")).toObject();
    SnapshotHeader decoded{schemaId(), {}, {1}, 0};
    if (version == 2) {
        const DocumentId id = DocumentId::parse(session.value(QStringLiteral("documentId")).toString());
        const auto revision = RevisionId::parseDecimal(session.value(QStringLiteral("revision")).toString());
        bool generationOk = false;
        const quint64 generation = session.value(QStringLiteral("resetGeneration")).toString().toULongLong(&generationOk);
        if (!id.isValid() || !revision || !generationOk)
            return failWith(QStringLiteral("Invalid design document identity or revision"));
        decoded.documentId = id;
        decoded.revision = *revision;
        decoded.resetGeneration = generation;
    } else {
        // AGENT-NOTE: a version 1 file (LocalDataDesignStore, WP-059 first
        // cut) gets a stable identity from its designId, so reopening it
        // twice names the same document; it is written back as version 2.
        decoded.documentId = {QUuid::createUuidV5(VersionOneNamespace,
                                                  root.value(QStringLiteral("designId")).toString())};
        root.insert(QStringLiteral("version"), 2);
    }
    const QString invalid = LocalDataDesignStore::checkStructure(root.toVariantMap());
    if (!invalid.isEmpty()) return failWith(invalid);
    if (design) *design = root;
    if (header) *header = decoded;
    if (extensions) *extensions = session.value(QStringLiteral("extensions")).toObject();
    return true;
}

QString LocalDataDesignDocument::toJson() const
{
    return QString::fromUtf8(encodeState(m_design, modelHeader(), {}));
}

// ------------------------------------------------------------ DocumentCodec
namespace {
class DesignCapture final : public DocumentCapture {
public:
    DesignCapture(SnapshotHeader header, QJsonObject design, QJsonObject extensions)
        : m_header(std::move(header)), m_design(std::move(design)), m_extensions(std::move(extensions)) {}
    SnapshotHeader header() const override { return m_header; }
    QByteArray encode(OperationError *error) const override
    {
        if (error) *error = {};
        return LocalDataDesignDocument::encodeState(m_design, m_header, m_extensions);
    }
private:
    SnapshotHeader m_header;
    QJsonObject m_design;
    QJsonObject m_extensions;
};
class DesignDecoded final : public DecodedDocument {
public:
    DesignDecoded(SnapshotHeader header, QJsonObject design, QJsonObject extensions)
        : m_header(std::move(header)), design(std::move(design)), m_extensions(std::move(extensions)) {}
    SnapshotHeader header() const override { return m_header; }
    QJsonObject extensions() const override { return m_extensions; }
    SnapshotHeader m_header;
    QJsonObject design;
    QJsonObject m_extensions;
};
} // namespace

class LocalDataDesignCodec final : public DocumentCodec {
public:
    QString schemaId() const override { return LocalDataDesignDocument::schemaId(); }
    QString kind() const override { return QStringLiteral("data-design"); }
    QString formatId() const override { return QString::fromLatin1(Format); }
    QString fileSuffix() const override { return QStringLiteral("qtkdata"); }
    qsizetype maximumEncodedBytes() const override { return LocalDataDesignDocument::MaximumBytes; }
    bool recognizes(const QByteArray &prefix) const override
    {
        // Compact session files and the indented version 1 store files.
        return prefix.trimmed().startsWith('{')
            && (prefix.contains("\"format\":\"qindatk.local-data-design\"")
                || prefix.contains("\"format\": \"qindatk.local-data-design\""));
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override
    {
        SnapshotHeader header;
        QString problem;
        if (!LocalDataDesignDocument::decodeState(bytes, nullptr, &header, nullptr, &problem)) {
            if (error) *error = codecError(ErrorCode::Validation, problem);
            return std::nullopt;
        }
        if (error) *error = {};
        return header;
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override
    {
        QJsonObject design, extensions;
        SnapshotHeader header;
        QString problem;
        if (!LocalDataDesignDocument::decodeState(bytes, &design, &header, &extensions, &problem)) {
            if (error) *error = codecError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const DesignDecoded>(header, std::move(design), extensions);
    }
    std::unique_ptr<DocumentModel> create() const override { return std::make_unique<LocalDataDesignDocument>(); }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded, OperationError *error) const override
    {
        const auto *source = dynamic_cast<const DesignDecoded *>(&decoded);
        if (!source) {
            if (error) *error = codecError(ErrorCode::InvalidArgument, QStringLiteral("Not a data design"));
            return {};
        }
        auto document = std::make_unique<LocalDataDesignDocument>();
        document->adoptLoaded(source->design, source->m_header);
        if (error) *error = {};
        return document;
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override
    {
        const auto *document = dynamic_cast<const LocalDataDesignDocument *>(&model);
        if (!document) {
            if (error) *error = codecError(ErrorCode::InvalidArgument, QStringLiteral("Not a data design"));
            return {};
        }
        if (QJsonDocument(extensions).toJson(QJsonDocument::Compact).size() > 64 * 1024) {
            if (error) *error = codecError(ErrorCode::Unsupported, QStringLiteral("Design extensions exceed 64 KiB"));
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const DesignCapture>(document->modelHeader(), document->state(), extensions);
    }
};

DocumentCodecPtr localDataDesignDocumentCodec()
{
    static const DocumentCodecPtr codec = std::make_shared<const LocalDataDesignCodec>();
    return codec;
}

} // namespace QindaTK
