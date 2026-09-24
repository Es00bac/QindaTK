// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_delimited.h"
#include "workbook_codec_p.h"
#include "workbook_format.h"

#include <QStringDecoder>
#include <QUuid>

namespace QindaTK {
using namespace Calc;
namespace {
constexpr qint64 MaxImportCells = 1000000;

OperationError formatError(const QString &formatId, ErrorCode code, const QString &message)
{
    return {code, message, {}, formatId};
}
bool allowedOptions(const QVariantMap &options, QString *unknown)
{
    static const QSet<QString> allowed{QStringLiteral("formulas"), QStringLiteral("sheetName"), QStringLiteral("sheet"),
        QStringLiteral("lineEnding"), QStringLiteral("byteOrderMark"), QStringLiteral("conflictPolicy"),
        QStringLiteral("expectedSha256"), QStringLiteral("expectedSourceSha256"), QStringLiteral("allowExtensionMismatch"),
        QStringLiteral("expectedSourceRevision"), QStringLiteral("expectedSnapshotSha256")};
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        if (!allowed.contains(it.key())) { *unknown = it.key(); return false; }
    return true;
}
// Strict UTF-8 without a byte-order mark.
bool decodeText(const QByteArray &bytes, QString *text)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    *text = decoder(bytes.startsWith("\xEF\xBB\xBF") ? bytes.mid(3) : bytes);
    return !decoder.hasError();
}
// The value a field holds: numbers, TRUE/FALSE and error literals typed as
// in a cell; formulas only on request; anything else is text as written.
CellContent fieldContent(const QString &field, bool formulas, bool *formulaLike)
{
    CellContent content;
    content.kind = CellContent::Kind::Literal;
    double number = 0.0;
    if (field.startsWith(QLatin1Char('=')) && field.size() > 1) {
        *formulaLike = true;
        if (formulas) {
            const ParseResult parsed = parseFormula(field, [](const QString &) { return QString(); });
            if (parsed.ok()) {
                content.kind = CellContent::Kind::Formula;
                content.formula = parsed.formula;
                return content;
            }
        }
        content.literal = Value::fromText(field);
    } else if (parseNumber(field, &number)) {
        content.literal = Value::fromNumber(number);
    } else if (field.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
               || field.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0) {
        content.literal = Value::fromBool(field.size() == 4);
    } else if (const ErrorKind error = errorFromText(field); error != ErrorKind::None) {
        content.literal = Value::fromError(error);
    } else {
        content.literal = Value::fromText(field.left(Workbook::MaxTextLength));
    }
    return content;
}
} // namespace

WorkbookDelimitedFormat::WorkbookDelimitedFormat(QChar delimiter) : m_delimiter(delimiter) {}
QString WorkbookDelimitedFormat::formatId() const
{ return QString::fromLatin1(m_delimiter == u'\t' ? TsvId : CsvId); }

bool WorkbookDelimitedFormat::parse(const QString &text, QChar delimiter, QList<QStringList> *rows, QString *error)
{
    rows->clear();
    QStringList row;
    QString field;
    bool quoted = false, fieldStarted = false;
    qint64 cells = 0;
    const qsizetype n = text.size();
    const auto endField = [&]() { row.append(field); field.clear(); fieldStarted = false; ++cells; };
    const auto endRow = [&]() {
        endField();
        rows->append(row);
        row.clear();
    };
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == u'"') {
                if (i + 1 < n && text[i + 1] == u'"') { field += c; ++i; }
                else quoted = false;
            } else {
                field += c;
            }
            continue;
        }
        if (c == u'"' && !fieldStarted && field.isEmpty()) { quoted = true; fieldStarted = true; }
        else if (c == delimiter) endField();
        else if (c == u'\r' || c == u'\n') {
            if (c == u'\r' && i + 1 < n && text[i + 1] == u'\n') ++i;
            endRow();
        } else { field += c; fieldStarted = true; }
        if (cells > MaxImportCells || rows->size() > MaxRows || row.size() > MaxColumns) {
            *error = QStringLiteral("The text exceeds %1 cells, %2 rows or %3 columns")
                         .arg(MaxImportCells).arg(MaxRows).arg(MaxColumns);
            return false;
        }
    }
    if (quoted) {
        *error = QStringLiteral("A quoted field is not closed");
        return false;
    }
    if (!field.isEmpty() || !row.isEmpty() || fieldStarted) endRow();
    return true;
}

QString WorkbookDelimitedFormat::quoted(const QString &field, QChar delimiter)
{
    const bool needs = field.contains(delimiter) || field.contains(QLatin1Char('"')) || field.contains(QLatin1Char('\n'))
        || field.contains(QLatin1Char('\r')) || field.startsWith(QLatin1Char(' ')) || field.endsWith(QLatin1Char(' '));
    if (!needs) return field;
    QString escaped = field;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QVariantMap WorkbookDelimitedFormat::metadata() const
{
    const bool tab = m_delimiter == u'\t';
    return {{"formatId", formatId()},
            {"title", tab ? QStringLiteral("Tab-separated values") : QStringLiteral("Comma-separated values")},
            {"mimeTypes", QStringList{tab ? QStringLiteral("text/tab-separated-values") : QStringLiteral("text/csv")}},
            {"extensions", tab ? QStringList{QStringLiteral("tsv"), QStringLiteral("tab")} : QStringList{QStringLiteral("csv")}},
            {"version", 1}, {"operations", QStringList{QStringLiteral("import"), QStringLiteral("export")}},
            {"editableImport", true}, {"renderOnly", false}, {"roundtrip", false},
            {"maximumBytes", Workbook::MaxFileBytes},
            {"features", QStringList{QStringLiteral("cells")}},
            {"losses", QStringList{QStringLiteral("formulas"), QStringLiteral("formats"), QStringLiteral("mergedCells"),
                                   QStringLiteral("conditionalFormats"), QStringLiteral("validation"),
                                   QStringLiteral("names"), QStringLiteral("sheetProperties"), QStringLiteral("otherSheets")}},
            {"optionKeys", QStringList{QStringLiteral("formulas"), QStringLiteral("sheetName"), QStringLiteral("sheet"),
                                       QStringLiteral("lineEnding"), QStringLiteral("byteOrderMark")}}};
}

bool WorkbookDelimitedFormat::recognizes(const QByteArray &prefix, const QUrl &url) const
{
    Q_UNUSED(prefix);
    const QString path = url.path();
    return m_delimiter == u'\t'
        ? path.endsWith(QStringLiteral(".tsv"), Qt::CaseInsensitive) || path.endsWith(QStringLiteral(".tab"), Qt::CaseInsensitive)
        : path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive);
}

FormatPlan WorkbookDelimitedFormat::preflightImport(const QByteArray &bytes, const QVariantMap &options) const
{
    FormatPlan plan;
    plan.options = options;
    QString unknown, text, error;
    QList<QStringList> rows;
    if (!allowedOptions(options, &unknown))
        plan.error = formatError(formatId(), ErrorCode::InvalidArgument, QStringLiteral("Unknown option '%1'").arg(unknown));
    else if (!decodeText(bytes, &text))
        plan.error = formatError(formatId(), ErrorCode::Validation, QStringLiteral("The text is not valid UTF-8"));
    else if (!parse(text, m_delimiter, &rows, &error))
        plan.error = formatError(formatId(), ErrorCode::Validation, error);
    if (plan.error) return plan;
    int columns = 0;
    for (const QStringList &row : std::as_const(rows)) columns = std::max(columns, int(row.size()));
    plan.preview = {{"rows", int(rows.size())}, {"columns", columns},
                    {"sheetName", options.value(QStringLiteral("sheetName"), QStringLiteral("Sheet1")).toString()}};
    return plan;
}

FormatPayload WorkbookDelimitedFormat::importDocument(const QByteArray &bytes, const QVariantMap &options,
                                                      const CancellationToken &cancel, const JobProgress &progress) const
{
    FormatPayload payload;
    const FormatPlan plan = preflightImport(bytes, options);
    if (plan.error) { payload.error = plan.error; return payload; }
    QString text, error;
    QList<QStringList> rows;
    decodeText(bytes, &text);
    parse(text, m_delimiter, &rows, &error);
    if (progress) progress(0.4);
    const QString sheetName = options.value(QStringLiteral("sheetName"), QStringLiteral("Sheet1")).toString();
    if (const QString problem = sheetNameProblem(sheetName); !problem.isEmpty()) {
        payload.error = formatError(formatId(), ErrorCode::InvalidArgument, problem);
        return payload;
    }
    const bool formulas = options.value(QStringLiteral("formulas")).toBool();
    DecodedWorkbook data;
    data.documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data.nextSheetNumber = 2;
    DecodedSheet sheet;
    sheet.id = QStringLiteral("sheet-1");
    sheet.name = sheetName;
    int formulaLike = 0, columns = 0;
    for (int r = 0; r < rows.size(); ++r) {
        if (cancel.isCancelled()) {
            payload.error = formatError(formatId(), ErrorCode::Cancelled, QStringLiteral("Cancelled"));
            return payload;
        }
        columns = std::max(columns, int(rows[r].size()));
        for (int c = 0; c < rows[r].size(); ++c) {
            if (rows[r][c].isEmpty()) continue;
            bool formula = false;
            CellContent content = fieldContent(rows[r][c], formulas, &formula);
            if (formula && content.kind != CellContent::Kind::Formula) ++formulaLike;
            sheet.cells.push_back({r, c, std::move(content)});
        }
    }
    sheet.rows = std::max<int>(Workbook::DefaultRows, int(rows.size()));
    sheet.columns = std::max(Workbook::DefaultColumns, columns);
    data.sheets.push_back(std::move(sheet));
    payload.bytes = encodeWorkbook(data);
    if (formulaLike > 0)
        payload.warnings.append(QStringLiteral("%1 fields starting with '=' were imported as text").arg(formulaLike));
    if (progress) progress(0.9);
    return payload;
}

FormatPlan WorkbookDelimitedFormat::preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const
{
    FormatPlan plan;
    plan.options = options;
    QString unknown, error;
    if (!allowedOptions(options, &unknown)) {
        plan.error = formatError(formatId(), ErrorCode::InvalidArgument, QStringLiteral("Unknown option '%1'").arg(unknown));
        return plan;
    }
    if (!snapshot || snapshot->header.schemaId != QLatin1StringView(WorkbookFormat::FormatId)) {
        plan.error = formatError(formatId(), ErrorCode::Unsupported, QStringLiteral("Snapshot is not a workbook"));
        return plan;
    }
    const auto decoded = decodeWorkbook(snapshot->payload, &error);
    if (!decoded) { plan.error = formatError(formatId(), ErrorCode::Validation, error); return plan; }
    const QString wanted = options.value(QStringLiteral("sheet")).toString();
    const DecodedSheet *chosen = nullptr;
    for (const DecodedSheet &sheet : decoded->sheets)
        if (wanted.isEmpty() ? !sheet.features->properties.hidden : sheet.name.compare(wanted, Qt::CaseInsensitive) == 0) {
            chosen = &sheet;
            break;
        }
    if (!chosen) {
        plan.error = formatError(formatId(), ErrorCode::InvalidArgument, QStringLiteral("No sheet '%1'").arg(wanted));
        return plan;
    }
    // Declared losses: only what this workbook actually has.
    if (decoded->formulaCount > 0) plan.losses.append(QStringLiteral("formulas"));
    if (decoded->sheets.size() > 1) plan.losses.append(QStringLiteral("otherSheets"));
    if (!decoded->names->empty()) plan.losses.append(QStringLiteral("names"));
    const SheetFeatures &f = *chosen->features;
    if (!f.formats.empty()) plan.losses.append(QStringLiteral("formats"));
    if (!f.merges.empty()) plan.losses.append(QStringLiteral("mergedCells"));
    if (!f.conditional.empty()) plan.losses.append(QStringLiteral("conditionalFormats"));
    if (!f.validations.empty()) plan.losses.append(QStringLiteral("validation"));
    if (!(f.properties == SheetProperties{})) plan.losses.append(QStringLiteral("sheetProperties"));
    plan.preview = {{"sheet", chosen->name}, {"sheetId", chosen->id}};
    return plan;
}

FormatPayload WorkbookDelimitedFormat::exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                                      const CancellationToken &cancel, const JobProgress &progress) const
{
    FormatPayload payload;
    const FormatPlan plan = preflightExport(snapshot, options);
    if (plan.error) { payload.error = plan.error; return payload; }
    payload.losses = plan.losses;
    // AGENT-NOTE: displayed values need calculation, so the job evaluates the
    // snapshot in its own Workbook (thread-affine to the job, no signals used).
    Workbook workbook;
    if (!workbook.restore(snapshot->payload)) {
        payload.error = formatError(formatId(), ErrorCode::Validation, workbook.lastError());
        return payload;
    }
    if (progress) progress(0.5);
    const QString sheetId = plan.preview.value(QStringLiteral("sheetId")).toString();
    // The used range: up to the last row and column holding a stored cell.
    const QVariantMap used = workbook.usedRange(sheetId);
    const int lastRow = used.value(QStringLiteral("lastRow"), -1).toInt();
    const int lastColumn = used.value(QStringLiteral("lastColumn"), -1).toInt();
    const bool tab = m_delimiter == u'\t';
    const QString lineEnding = options.value(QStringLiteral("lineEnding"), tab ? QStringLiteral("lf") : QStringLiteral("crlf"))
                                   .toString() == u"lf" ? QStringLiteral("\n") : QStringLiteral("\r\n");
    QString out;
    for (int r = 0; r <= lastRow; ++r) {
        if (cancel.isCancelled()) {
            payload.error = formatError(formatId(), ErrorCode::Cancelled, QStringLiteral("Cancelled"));
            return payload;
        }
        QStringList fields;
        for (int c = 0; c <= lastColumn; ++c) fields.append(quoted(workbook.cellDisplay(sheetId, r, c), m_delimiter));
        out += fields.join(m_delimiter) + lineEnding;
    }
    payload.bytes = (options.value(QStringLiteral("byteOrderMark")).toBool() ? QByteArray("\xEF\xBB\xBF") : QByteArray())
                    + out.toUtf8();
    if (progress) progress(0.95);
    return payload;
}

} // namespace QindaTK
