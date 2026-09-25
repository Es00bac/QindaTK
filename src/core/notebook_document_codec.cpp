// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_codec.h"
#include "notebook_document_p.h"
#include "undo_service.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

// Native notebook file (docs/notebooks.md#format): one JSON object,
// "format": "QindaTK.Notebook", version 1, carrying the session header.
namespace QindaTK {
using namespace Notebook;

namespace {
constexpr auto FormatName = "QindaTK.Notebook";

bool validId(const QString &id) { return !id.isEmpty() && id.size() <= 128 && !id.contains(QChar::Null); }

QJsonObject encodeBlock(const Block &block)
{
    QJsonObject record{{"id", block.id}, {"type", block.type}, {"data", block.data}};
    if (!block.parentId.isEmpty()) record.insert(QStringLiteral("parentId"), block.parentId);
    if (block.locked) record.insert(QStringLiteral("locked"), true);
    return record;
}

OperationError codecError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("notebook.codec")};
}
} // namespace

QByteArray NotebookDocument::encodeNative(const QJsonObject &extensions) const
{
    QJsonArray sections, pages;
    for (const Section &section : m_state.sections) {
        QJsonObject record{{"id", section.id}, {"title", section.title}};
        if (m_collapsed.contains(section.id)) record.insert(QStringLiteral("collapsed"), true);
        sections.append(record);
    }
    for (const Page &page : m_state.pages) {
        QJsonArray blocks;
        for (const Block &block : page.blocks) blocks.append(encodeBlock(block));
        pages.append(QJsonObject{{"id", page.id}, {"sectionId", page.sectionId}, {"title", page.title},
                                 {"tags", QJsonArray::fromStringList(page.tags)}, {"blocks", blocks}});
    }
    const SnapshotHeader header = modelHeader();
    return QJsonDocument(QJsonObject{{"format", QString::fromLatin1(FormatName)}, {"version", 1},
        {"documentId", header.documentId.toString()}, {"revision", QString::number(header.revision.value)},
        {"resetGeneration", QString::number(header.resetGeneration)}, {"title", m_state.title},
        {"sections", sections}, {"pages", pages}, {"extensions", extensions}}).toJson(QJsonDocument::Compact);
}

bool NotebookDocument::decodeNative(const QByteArray &bytes, State *state, SnapshotHeader *header,
                                    QJsonObject *extensions, QSet<QString> *collapsed, QString *problem)
{
    auto reject = [problem](const QString &message) {
        if (problem) *problem = message;
        return false;
    };
    if (bytes.size() > Limits::MaxFileBytes) return reject(QStringLiteral("Notebook exceeds 64 MiB"));
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return reject(QStringLiteral("Notebook is not a JSON object: %1").arg(parse.errorString()));
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(FormatName))
        return reject(QStringLiteral("Not a QindaTK notebook"));
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return reject(QStringLiteral("Unsupported notebook version %1").arg(root.value(QStringLiteral("version")).toInt()));
    const DocumentId id = DocumentId::parse(root.value(QStringLiteral("documentId")).toString());
    const auto revision = RevisionId::parseDecimal(root.value(QStringLiteral("revision")).toString());
    bool generationOk = false;
    const quint64 generation = root.value(QStringLiteral("resetGeneration")).toString().toULongLong(&generationOk);
    if (!id.isValid() || !revision || !generationOk)
        return reject(QStringLiteral("Notebook header needs a document ID, revision and reset generation"));

    State decoded;
    QSet<QString> folded, ids;
    decoded.title = root.value(QStringLiteral("title")).toString().left(512);
    const QJsonArray sections = root.value(QStringLiteral("sections")).toArray();
    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    if (sections.size() > Limits::MaxSections || pages.size() > Limits::MaxPages)
        return reject(QStringLiteral("Notebook exceeds the section or page limit"));
    QSet<QString> sectionIds;
    for (const QJsonValue &value : sections) {
        const QJsonObject record = value.toObject();
        const Section section{record.value(QStringLiteral("id")).toString(),
                              record.value(QStringLiteral("title")).toString().left(512)};
        if (!validId(section.id) || ids.contains(section.id))
            return reject(QStringLiteral("Section ID %1 is invalid or repeated").arg(section.id));
        ids.insert(section.id);
        sectionIds.insert(section.id);
        if (record.value(QStringLiteral("collapsed")).toBool()) folded.insert(section.id);
        decoded.sections.append(section);
    }
    int total = 0;
    for (const QJsonValue &value : pages) {
        const QJsonObject record = value.toObject();
        Page page;
        page.id = record.value(QStringLiteral("id")).toString();
        page.sectionId = record.value(QStringLiteral("sectionId")).toString();
        page.title = record.value(QStringLiteral("title")).toString().left(512);
        if (!validId(page.id) || ids.contains(page.id))
            return reject(QStringLiteral("Page ID %1 is invalid or repeated").arg(page.id));
        if (!sectionIds.contains(page.sectionId))
            return reject(QStringLiteral("Page %1 names unknown section %2").arg(page.id, page.sectionId));
        ids.insert(page.id);
        for (const QJsonValue &tag : record.value(QStringLiteral("tags")).toArray()) page.tags.append(tag.toString().left(128));
        const QJsonArray blocks = record.value(QStringLiteral("blocks")).toArray();
        total += int(blocks.size());
        if (blocks.size() > Limits::MaxBlocksPerPage || total > Limits::MaxBlocks)
            return reject(QStringLiteral("Notebook exceeds the block limit"));
        page.blocks.reserve(blocks.size());
        for (const QJsonValue &blockValue : blocks) {
            const QJsonObject item = blockValue.toObject();
            Block block{item.value(QStringLiteral("id")).toString(), item.value(QStringLiteral("type")).toString(),
                        item.value(QStringLiteral("parentId")).toString(), item.value(QStringLiteral("data")).toObject(),
                        item.value(QStringLiteral("locked")).toBool()};
            if (!validId(block.id) || ids.contains(block.id))
                return reject(QStringLiteral("Block ID %1 is invalid or repeated").arg(block.id));
            if (block.type.isEmpty() || block.type.size() > 64)
                return reject(QStringLiteral("Block %1 has no valid type").arg(block.id));
            // Unknown types are preserved verbatim (docs/notebooks.md#blocks).
            if (NotebookText::hasRuns(block.type)) {
                const QString runs = NotebookText::validateRuns(block.data);
                if (!runs.isEmpty()) return reject(QStringLiteral("Block %1: %2").arg(block.id, runs));
            }
            if (NotebookText::isTextType(block.type)
                && block.data.value(QStringLiteral("text")).toString().size() > Limits::MaxTextLength)
                return reject(QStringLiteral("Block %1 text exceeds the limit").arg(block.id));
            ids.insert(block.id);
            page.blocks.append(block);
        }
        const QString tree = NotebookTree::validate(page.blocks);
        if (!tree.isEmpty()) return reject(tree);
        decoded.pages.append(page);
    }
    if (state) *state = std::move(decoded);
    if (collapsed) *collapsed = folded;
    if (header) *header = {QString::fromLatin1(SchemaId), id, *revision, generation};
    if (extensions) *extensions = root.value(QStringLiteral("extensions")).toObject();
    if (problem) problem->clear();
    return true;
}

QString NotebookDocument::toJson() const
{
    return QString::fromUtf8(encodeNative({}));
}

bool NotebookDocument::loadJson(const QString &json)
{
    State incoming;
    QSet<QString> collapsed;
    QString problem;
    if (!decodeNative(json.toUtf8(), &incoming, nullptr, nullptr, &collapsed, &problem)) return fail(problem);
    if (!commit(incoming, tr("Load notebook"))) return false;
    m_collapsed = collapsed;
    emit changed();
    return true;
}

bool NotebookDocument::saveToFile(const QString &path)
{
    QSaveFile file(path);
    const QByteArray bytes = encodeNative({});
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("Cannot save notebook: %1").arg(file.errorString()));
    clearError();
    return true;
}

bool NotebookDocument::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > Limits::MaxFileBytes)
        return fail(QStringLiteral("Cannot read notebook or file exceeds 64 MiB"));
    return loadJson(QString::fromUtf8(file.readAll()));
}

namespace {
class NotebookCapture final : public DocumentCapture {
public:
    NotebookCapture(SnapshotHeader header, QByteArray bytes) : m_header(std::move(header)), m_bytes(std::move(bytes)) {}
    SnapshotHeader header() const override { return m_header; }
    QByteArray encode(OperationError *error) const override { if (error) *error = {}; return m_bytes; }
private:
    SnapshotHeader m_header;
    QByteArray m_bytes;
};
class NotebookDecoded final : public DecodedDocument {
public:
    SnapshotHeader header() const override { return headerValue; }
    QJsonObject extensions() const override { return extensionValues; }
    SnapshotHeader headerValue;
    QJsonObject extensionValues;
    Notebook::State state;
    QSet<QString> collapsed;
};
} // namespace

// AGENT-CONTRACT (docs/document-models.md#codecs): schema qindatk.notebook.v1,
// suffix qtknb. decode() validates everything and keeps the plain state, so
// adopt() only moves it into a fresh model with a clean history.
class NotebookDocumentCodec final : public DocumentCodec {
public:
    QString schemaId() const override { return QString::fromLatin1(NotebookDocument::SchemaId); }
    QString kind() const override { return QStringLiteral("notebook"); }
    QString formatId() const override { return QString::fromLatin1(FormatName); }
    QString fileSuffix() const override { return QStringLiteral("qtknb"); }
    qsizetype maximumEncodedBytes() const override { return Limits::MaxFileBytes; }
    bool recognizes(const QByteArray &prefix) const override
    {
        return prefix.trimmed().startsWith('{') && prefix.contains(FormatName);
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override
    {
        SnapshotHeader header;
        QString problem;
        if (!NotebookDocument::decodeNative(bytes, nullptr, &header, nullptr, nullptr, &problem)) {
            if (error) *error = codecError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return header;
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override
    {
        auto decoded = std::make_shared<NotebookDecoded>();
        QString problem;
        if (!NotebookDocument::decodeNative(bytes, &decoded->state, &decoded->headerValue,
                                            &decoded->extensionValues, &decoded->collapsed, &problem)) {
            if (error) *error = codecError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return decoded;
    }
    std::unique_ptr<DocumentModel> create() const override { return std::make_unique<NotebookDocument>(); }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded, OperationError *error) const override
    {
        const auto *source = dynamic_cast<const NotebookDecoded *>(&decoded);
        if (!source) {
            if (error) *error = codecError(ErrorCode::InvalidArgument, QStringLiteral("Not a notebook document"));
            return {};
        }
        auto notebook = std::make_unique<NotebookDocument>();
        notebook->m_localHistory->removeModel(notebook.get());
        notebook->m_documentId = source->headerValue.documentId.toString();
        notebook->m_revision = source->headerValue.revision.value;
        notebook->m_resetGeneration = source->headerValue.resetGeneration;
        notebook->m_state = source->state;
        notebook->m_collapsed = source->collapsed;
        notebook->m_localHistory->addModel(notebook.get());
        notebook->m_localHistory->setStepLimit(notebook.get(), 200);
        notebook->m_localHistory->markModelClean(notebook.get());
        if (error) *error = {};
        return notebook;
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override
    {
        const auto *notebook = dynamic_cast<const NotebookDocument *>(&model);
        if (!notebook) {
            if (error) *error = codecError(ErrorCode::InvalidArgument, QStringLiteral("Not a notebook model"));
            return {};
        }
        const QByteArray bytes = notebook->encodeNative(extensions);
        if (bytes.size() > maximumEncodedBytes()) {
            if (error) *error = codecError(ErrorCode::Unsupported, QStringLiteral("Notebook exceeds 64 MiB"));
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const NotebookCapture>(notebook->modelHeader(), bytes);
    }
};

DocumentCodecPtr notebookDocumentCodec()
{
    static const DocumentCodecPtr codec = std::make_shared<const NotebookDocumentCodec>();
    return codec;
}

} // namespace QindaTK
