// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_codec.h"
#include "workbook_codec_p.h"
#include "workbook_format.h"

#include <QJsonDocument>

// qindatk.workbook.v1 as a DocumentCodec (docs/document-models.md#codecs,
// docs/workbook.md#sessions): DocumentManager, DocumentSession, recovery,
// workspace restore and FormatRegistry.adoptImport open, save and recover
// workbooks through it. Files it writes carry the session header (revision,
// reset generation and extensions); plain Workbook.serialize() files have
// none and open as revision 1.
namespace QindaTK {
namespace {
using namespace Calc;

SnapshotHeader headerOf(const DecodedWorkbook &data)
{
    return {QString::fromLatin1(WorkbookFormat::FormatId), DocumentId::parse(data.documentId),
            {data.hasHeader ? data.revision : 1}, data.hasHeader ? data.resetGeneration : 0};
}
OperationError failure(ErrorCode code, const QString &message)
{
    return {code, message, {}, QString::fromLatin1(WorkbookFormat::FormatId)};
}

class WorkbookDecodedDocument final : public DecodedDocument {
public:
    explicit WorkbookDecodedDocument(DecodedWorkbook data) : data(std::move(data)) {}
    SnapshotHeader header() const override { return headerOf(data); }
    QJsonObject extensions() const override { return data.extensions; }
    const DecodedWorkbook data;
};

// Holds a plain copy of the content (shared immutable cells, formulas and
// features); encoding runs on the save or autosave worker.
class WorkbookCapture final : public DocumentCapture {
public:
    WorkbookCapture(SnapshotHeader header, DecodedWorkbook data) : m_header(std::move(header)), m_data(std::move(data)) {}
    SnapshotHeader header() const override { return m_header; }
    QByteArray encode(OperationError *error) const override
    {
        if (error) *error = {};
        return encodeWorkbook(m_data);
    }

private:
    SnapshotHeader m_header;
    DecodedWorkbook m_data;
};

class WorkbookDocumentCodec final : public DocumentCodec {
public:
    QString schemaId() const override { return QString::fromLatin1(WorkbookFormat::FormatId); }
    QString kind() const override { return QStringLiteral("workbook"); }
    QString formatId() const override { return QString::fromLatin1(WorkbookFormat::FormatId); }
    QString fileSuffix() const override { return QStringLiteral("qtkwb"); }
    qsizetype maximumEncodedBytes() const override { return Workbook::MaxFileBytes; }
    bool recognizes(const QByteArray &prefix) const override
    {
        const QByteArray trimmed = prefix.trimmed();
        return trimmed.startsWith('{') && trimmed.contains("\"format\":\"qindatk.workbook\"");
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override
    {
        const auto decoded = decodeChecked(bytes, error);
        if (!decoded) return std::nullopt;
        return headerOf(*decoded);
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override
    {
        auto decoded = decodeChecked(bytes, error);
        if (!decoded) return {};
        return std::make_shared<const WorkbookDecodedDocument>(std::move(*decoded));
    }
    std::unique_ptr<DocumentModel> create() const override { return std::make_unique<Workbook>(); }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded, OperationError *error) const override
    {
        const auto *document = dynamic_cast<const WorkbookDecodedDocument *>(&decoded);
        if (!document) {
            if (error) *error = failure(ErrorCode::InvalidArgument, QStringLiteral("The decoded document is not a workbook"));
            return {};
        }
        auto workbook = std::make_unique<Workbook>();
        DecodedWorkbook data = document->data;
        WorkbookPrivate::loadState(*workbook, std::move(data), document->header());
        if (error) *error = {};
        return workbook;
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override
    {
        const auto *workbook = dynamic_cast<const Workbook *>(&model);
        if (!workbook) {
            if (error) *error = failure(ErrorCode::InvalidArgument, QStringLiteral("The model is not a workbook"));
            return {};
        }
        if (workbook->transactionActive()) {
            if (error) *error = failure(ErrorCode::Unavailable, QStringLiteral("Finish the active workbook transaction first"));
            return {};
        }
        if (QJsonDocument(extensions).toJson(QJsonDocument::Compact).size() > MaxExtensionBytes) {
            if (error) *error = failure(ErrorCode::Unsupported, QStringLiteral("Workbook extensions exceed the size limit"));
            return {};
        }
        const SnapshotHeader header = workbook->modelHeader();
        DecodedWorkbook data = WorkbookPrivate::captureState(*workbook);
        data.hasHeader = true;
        data.revision = header.revision.value;
        data.resetGeneration = header.resetGeneration;
        data.extensions = extensions;
        if (error) *error = {};
        return std::make_shared<const WorkbookCapture>(header, std::move(data));
    }

private:
    static std::optional<DecodedWorkbook> decodeChecked(const QByteArray &bytes, OperationError *error)
    {
        QString problem;
        auto decoded = decodeWorkbook(bytes, &problem);
        if (!decoded) {
            if (error) *error = failure(ErrorCode::Validation, problem);
            return std::nullopt;
        }
        if (error) *error = {};
        return decoded;
    }
};
} // namespace

DocumentCodecPtr workbookDocumentCodec()
{
    static const DocumentCodecPtr codec = std::make_shared<const WorkbookDocumentCodec>();
    return codec;
}

} // namespace QindaTK
