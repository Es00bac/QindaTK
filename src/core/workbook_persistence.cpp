// SPDX-License-Identifier: LGPL-3.0-or-later
#include "format_io.h"
#include "undo_service.h"
#include "workbook_codec_p.h"
#include "workbook_delimited.h"
#include "workbook_format.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace QindaTK {
using namespace Calc;
namespace {
OperationError formatError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QString::fromLatin1(WorkbookFormat::FormatId)};
}
bool allowedOptions(const QVariantMap &options)
{
    static const QSet<QString> allowed{QStringLiteral("conflictPolicy"), QStringLiteral("expectedSha256"),
        QStringLiteral("expectedSourceSha256"), QStringLiteral("allowExtensionMismatch"),
        QStringLiteral("expectedSourceRevision"), QStringLiteral("expectedSnapshotSha256")};
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
QVariantMap preview(const DecodedWorkbook &data)
{
    QStringList names;
    for (const DecodedSheet &sheet : data.sheets) names.append(sheet.name);
    int formatted = 0;
    for (const DecodedSheet &sheet : data.sheets) formatted += sheet.features->isEmpty() ? 0 : 1;
    return {{"documentId", data.documentId}, {"sheetCount", int(data.sheets.size())},
            {"sheetNames", names}, {"cellCount", data.cellCount}, {"formulaCount", data.formulaCount},
            {"nameCount", int(data.names->size())}, {"sheetsWithFeatures", formatted}};
}
FormatPlan planFor(const QByteArray &bytes, const QVariantMap &options)
{
    FormatPlan plan;
    plan.options = options;
    if (!allowedOptions(options)) {
        plan.error = formatError(ErrorCode::InvalidArgument, QStringLiteral("Unknown workbook format option"));
        return plan;
    }
    QString error;
    const auto decoded = decodeWorkbook(bytes, &error);
    if (!decoded) { plan.error = formatError(ErrorCode::Validation, error); return plan; }
    plan.preview = preview(*decoded);
    return plan;
}
FormatPayload canonical(const QByteArray &bytes, const CancellationToken &cancel, const JobProgress &progress)
{
    FormatPayload payload;
    if (cancel.isCancelled()) { payload.error = formatError(ErrorCode::Cancelled, QStringLiteral("Cancelled")); return payload; }
    QString error;
    const auto decoded = decodeWorkbook(bytes, &error);
    if (!decoded) { payload.error = formatError(ErrorCode::Validation, error); return payload; }
    if (progress) progress(0.6);
    payload.bytes = encodeWorkbook(*decoded);
    return payload;
}
} // namespace

QVariantMap WorkbookFormat::metadata() const
{
    return {{"formatId", QString::fromLatin1(FormatId)}, {"title", QStringLiteral("QindaTK workbook")},
            {"mimeTypes", QStringList{QStringLiteral("application/vnd.qindatk.workbook+json")}},
            {"extensions", QStringList{QStringLiteral("qtkwb")}}, {"version", WorkbookFormatVersion},
            {"operations", QStringList{QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("roundtrip")}},
            {"editableImport", true}, {"renderOnly", false}, {"roundtrip", true},
            {"maximumBytes", Workbook::MaxFileBytes},
            {"schemas", QStringList{QString::fromLatin1(FormatId)}},
            {"features", QStringList{QStringLiteral("sheets"), QStringLiteral("cells"), QStringLiteral("formulas"),
                                     QStringLiteral("names"), QStringLiteral("formats"), QStringLiteral("merges"),
                                     QStringLiteral("conditionalFormats"), QStringLiteral("validations"),
                                     QStringLiteral("sheetProperties")}},
            {"optionKeys", QStringList{QStringLiteral("conflictPolicy"), QStringLiteral("expectedSha256"),
                                        QStringLiteral("expectedSourceSha256"),
                                        QStringLiteral("expectedSourceRevision"),
                                        QStringLiteral("expectedSnapshotSha256")}}};
}
bool WorkbookFormat::recognizes(const QByteArray &prefix, const QUrl &url) const
{
    return prefix.contains("\"format\":\"qindatk.workbook\"")
        || url.path().endsWith(QStringLiteral(".qtkwb"), Qt::CaseInsensitive);
}
FormatPlan WorkbookFormat::preflightImport(const QByteArray &bytes, const QVariantMap &options) const
{ return planFor(bytes, options); }
FormatPayload WorkbookFormat::importDocument(const QByteArray &bytes, const QVariantMap &options,
                                             const CancellationToken &cancel, const JobProgress &progress) const
{
    Q_UNUSED(options);
    return canonical(bytes, cancel, progress);
}
FormatPlan WorkbookFormat::preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const
{
    if (!snapshot || snapshot->header.schemaId != QLatin1StringView(FormatId)) {
        FormatPlan plan;
        plan.options = options;
        plan.error = formatError(ErrorCode::Unsupported, QStringLiteral("Snapshot is not a workbook"));
        return plan;
    }
    return planFor(snapshot->payload, options);
}
FormatPayload WorkbookFormat::exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                             const CancellationToken &cancel, const JobProgress &progress) const
{
    Q_UNUSED(options);
    if (!snapshot || snapshot->header.schemaId != QLatin1StringView(FormatId)) {
        FormatPayload payload;
        payload.error = formatError(ErrorCode::Unsupported, QStringLiteral("Snapshot is not a workbook"));
        return payload;
    }
    return canonical(snapshot->payload, cancel, progress);
}

void WorkbookPrivate::adopt(DecodedWorkbook &&data, const SnapshotHeader *header)
{
    // AGENT-GUARD: UndoService keys a history by document ID. A new identity
    // leaves the private service first and re-attaches under the new key.
    const bool identityChanges = data.documentId != documentId;
    if (identityChanges) {
        Q_ASSERT(standalone());
        if (UndoService *service = q->historyService()) service->removeModel(q);
    }
    sheets.clear();
    byId.clear();
    byUid.clear();
    documentId = data.documentId;
    nextSheetNumber = data.nextSheetNumber;
    names = data.names;
    scaleCache.clear();
    QSet<quint64> seeds;
    for (DecodedSheet &decoded : data.sheets) {
        Sheet *s = createSheet(decoded.id, decoded.name, -1, decoded.rows, decoded.columns);
        s->features = decoded.features;
        for (CellEntry &entry : decoded.cells) {
            const quint64 position = positionKey(entry.row, entry.column);
            Cell cell;
            cell.value = entry.content.kind == CellContent::Kind::Literal ? entry.content.literal : Value::blank();
            if (entry.content.kind == CellContent::Kind::Formula) {
                ++s->formulaCount;
                seeds.insert(globalKey(s->uid, position));
            }
            cell.content = std::move(entry.content);
            s->cells.emplace_hint(s->cells.end(), position, std::move(cell));
        }
    }
    rebuildDependencies();
    recalculate(seeds, nullptr);
    if (header) {
        revision = header->revision.value;
        resetGeneration = header->resetGeneration;
    } else {
        ++revision;
        ++resetGeneration;
    }
    if (identityChanges) attachLocalHistory();
    else q->invalidateHistory();
    lastError.clear();
    lastErrorDetail.clear();
    emit q->resetOccurred();
    emit q->sheetsChanged();
    emit q->namesChanged();
    emit q->revisionChanged();
    emit q->historyChanged();
    emit q->errorChanged();
}

QUrl Workbook::fileUrl() const { return d->fileUrl; }

DecodedWorkbook WorkbookPrivate::decodedState() const
{
    // AGENT-NOTE: cell contents, formulas, features and names are immutable
    // once shared, so the copy can be encoded on a worker while edits go on.
    DecodedWorkbook data;
    data.documentId = documentId;
    data.nextSheetNumber = nextSheetNumber;
    data.names = names;
    for (const auto &s : sheets) {
        DecodedSheet sheet{s->id, s->name, s->rows, s->columns, {}, s->features};
        sheet.cells.reserve(s->cells.size());
        for (const auto &[position, c] : s->cells)
            sheet.cells.push_back({keyRow(position), keyColumn(position), c.content});
        data.sheets.push_back(std::move(sheet));
    }
    return data;
}
bool WorkbookPrivate::standalone() const
{
    const UndoService *service = q->historyService();
    return !service || service == localHistory;
}
DecodedWorkbook WorkbookPrivate::captureState(const Workbook &workbook) { return workbook.d->decodedState(); }
void WorkbookPrivate::loadState(Workbook &workbook, DecodedWorkbook &&data, const SnapshotHeader &header)
{
    workbook.d->adopt(std::move(data), &header);
    workbook.d->localHistory->markModelClean(&workbook);
}

QByteArray Workbook::serialize() const
{
    if (d->txn) return {};
    return encodeWorkbook(d->decodedState());
}
bool Workbook::restore(const QByteArray &bytes)
{
    if (d->txn) return d->fail(QStringLiteral("Finish the active workbook transaction first"));
    QString error;
    auto decoded = decodeWorkbook(bytes, &error);
    if (!decoded) return d->fail(error);
    if (decoded->documentId != d->documentId && !d->standalone())
        return d->fail(QStringLiteral("A workbook in a document session keeps its identity; open the file "
                                      "through the DocumentManager"));
    const bool standalone = d->standalone();
    d->adopt(std::move(*decoded));
    // Standalone, the restored content is the saved baseline; in a session
    // the session's file still holds the previous content.
    if (standalone) markSaved();
    else markUnsaved();
    return true;
}
SnapshotPtr Workbook::snapshot() const
{
    auto snapshot = std::make_shared<ImmutableSnapshot>();
    snapshot->header.schemaId = QString::fromLatin1(WorkbookFormat::FormatId);
    snapshot->header.documentId = DocumentId::parse(d->documentId);
    snapshot->header.revision.value = d->revision;
    snapshot->header.resetGeneration = d->resetGeneration;
    snapshot->payload = serialize();
    return snapshot;
}
QString Workbook::serializedText() const { return QString::fromUtf8(serialize()); }
bool Workbook::restoreText(const QString &text) { return restore(text.toUtf8()); }

bool Workbook::save(const QUrl &url, bool replaceExisting)
{
    if (d->txn) return d->fail(QStringLiteral("Finish the active workbook transaction first"));
    QString path;
    if (const OperationError error = FormatIO::localPath(url, &path)) return d->fail(error.message);
    QVariantMap options;
    if (QFileInfo::exists(path)) {
        QByteArray expected;
        if (url == d->fileUrl && !d->fileSha256.isEmpty()) {
            expected = d->fileSha256;
        } else if (replaceExisting) {
            QFile current(path);
            if (!current.open(QIODevice::ReadOnly)) return d->fail(current.errorString());
            expected = FormatIO::hash(current.readAll());
        } else {
            return d->fail(QStringLiteral("The file already exists"), {{"path", path}});
        }
        options = {{"conflictPolicy", QStringLiteral("replaceIfHash")},
                   {"expectedSha256", QString::fromLatin1(expected.toHex())}};
    }
    const QByteArray bytes = serialize();
    QByteArray sha256;
    const CancellationSource never;
    if (const OperationError error = FormatIO::writeAtomic(path, bytes, options, never.token(),
                                                           [](double) {}, &sha256))
        return d->fail(error.message, {{"path", path}});
    d->fileUrl = url;
    d->fileSha256 = sha256;
    d->clearError();
    markSaved();
    emit fileChanged();
    return true;
}
bool Workbook::open(const QUrl &url)
{
    if (d->txn) return d->fail(QStringLiteral("Finish the active workbook transaction first"));
    QString path;
    if (const OperationError error = FormatIO::localPath(url, &path)) return d->fail(error.message);
    QByteArray bytes;
    if (const OperationError error = FormatIO::readBounded(path, MaxFileBytes, &bytes))
        return d->fail(error.message, {{"path", path}});
    if (!restore(bytes)) return false;
    d->fileUrl = url;
    d->fileSha256 = FormatIO::hash(bytes);
    emit fileChanged();
    return true;
}
void Workbook::reset()
{
    if (!d->standalone()) {
        d->fail(QStringLiteral("A workbook in a document session keeps its identity; create a new document "
                               "through the DocumentManager"));
        return;
    }
    if (d->txn) d->cancel();
    DecodedWorkbook data;
    data.documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data.nextSheetNumber = 2;
    data.sheets.push_back({QStringLiteral("sheet-1"), QStringLiteral("Sheet1"), DefaultRows, DefaultColumns, {}});
    d->adopt(std::move(data));
    markSaved(); // a new, empty document has nothing unsaved
    d->fileUrl.clear();
    d->fileSha256.clear();
    emit fileChanged();
}
bool Workbook::registerFormat(QObject *formatRegistry) const
{
    auto *registry = qobject_cast<FormatRegistry *>(formatRegistry);
    if (!registry) return false;
    const bool native = registry->registerProvider(std::make_shared<WorkbookFormat>());
    const bool csv = registry->registerProvider(std::make_shared<WorkbookDelimitedFormat>(QLatin1Char(',')));
    const bool tsv = registry->registerProvider(std::make_shared<WorkbookDelimitedFormat>(QLatin1Char('\t')));
    return native && csv && tsv;
}
bool Workbook::adoptImport(QObject *formatRegistry, const QString &jobId)
{
    auto *registry = qobject_cast<FormatRegistry *>(formatRegistry);
    if (!registry) return d->fail(QStringLiteral("No format registry"));
    const QVariantMap record = registry->jobs()->job(jobId);
    const QVariantMap result = record.value(QStringLiteral("result")).toMap();
    const QString formatId = result.value(QStringLiteral("formatId")).toString();
    if (record.value(QStringLiteral("state")).toString() != u"succeeded" || result.value(QStringLiteral("operation")).toString() != u"import")
        return d->fail(QStringLiteral("The import job has not succeeded"));
    if (formatId != QLatin1StringView(WorkbookFormat::FormatId) && formatId != QLatin1StringView(WorkbookDelimitedFormat::CsvId)
        && formatId != QLatin1StringView(WorkbookDelimitedFormat::TsvId))
        return d->fail(QStringLiteral("The import is not a workbook"));
    if (!restore(result.value(QStringLiteral("documentBytes")).toByteArray())) return false;
    d->fileUrl.clear();
    d->fileSha256.clear();
    markUnsaved(); // imported content is not saved in the native format yet
    emit fileChanged();
    return true;
}

} // namespace QindaTK
