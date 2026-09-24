// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_codec_p.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace QindaTK::Calc {
namespace {
// AGENT-NOTE: stored formulas can render longer than typed ones after
// structural rewrites lengthen addresses; reopening must accept them.
constexpr int StoredFormulaLength = 8 * MaxFormulaLength;
constexpr qsizetype MaxSheetIdLength = 128;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool integerValue(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 *out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number != std::floor(number) || number < double(minimum)
        || number > double(maximum)) return false;
    *out = qint64(number);
    return true;
}
bool onlyKeys(const QJsonObject &object, std::initializer_list<const char *> keys, QString *unknown,
              bool withFeatures = false)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto matches = [&](const char *key) { return it.key() == QLatin1StringView(key); };
        const bool known = std::any_of(keys.begin(), keys.end(), matches)
            || (withFeatures && std::any_of(std::begin(SheetFeatureKeys), std::end(SheetFeatureKeys), matches));
        if (!known) { *unknown = it.key(); return false; }
    }
    return true;
}
QJsonArray encodeCell(const CellEntry &e, const SheetNameLookup &names)
{
    QJsonArray cell{cellAddress(e.row, e.column)};
    if (e.content.kind == CellContent::Kind::Formula) {
        cell.append(QStringLiteral("f"));
        cell.append(renderFormula(*e.content.formula, names));
        return cell;
    }
    const Value &v = e.content.literal;
    switch (v.type) {
    case Value::Type::Number: cell.append(QStringLiteral("n")); cell.append(v.number); break;
    case Value::Type::Text: cell.append(QStringLiteral("s")); cell.append(v.text); break;
    case Value::Type::Boolean: cell.append(QStringLiteral("b")); cell.append(v.boolean); break;
    case Value::Type::Error: cell.append(QStringLiteral("e")); cell.append(errorText(v.error)); break;
    case Value::Type::Blank: break;
    }
    return cell;
}
// The optional session header: decimal-string revision and reset generation
// (both or neither) and an extensions object.
bool decodeSessionHeader(const QJsonObject &root, DecodedWorkbook *result, QString *error)
{
    const QJsonValue revision = root.value(QStringLiteral("revision"));
    const QJsonValue generation = root.value(QStringLiteral("resetGeneration"));
    const QJsonValue extensions = root.value(QStringLiteral("extensions"));
    if (revision.isUndefined() != generation.isUndefined())
        return fail(error, QStringLiteral("A workbook header needs both revision and resetGeneration"));
    if (!revision.isUndefined()) {
        const auto parsedRevision = RevisionId::parseDecimal(revision.toString());
        bool generationOk = false;
        const quint64 parsedGeneration = generation.toString().toULongLong(&generationOk, 10);
        if (!revision.isString() || !parsedRevision || !generation.isString() || !generationOk
            || QString::number(parsedGeneration) != generation.toString())
            return fail(error, QStringLiteral("Workbook revision and resetGeneration must be decimal strings"));
        result->hasHeader = true;
        result->revision = parsedRevision->value;
        result->resetGeneration = parsedGeneration;
    }
    if (!extensions.isUndefined()) {
        if (!extensions.isObject() || !result->hasHeader)
            return fail(error, QStringLiteral("Workbook extensions must be an object in a file with a header"));
        if (QJsonDocument(extensions.toObject()).toJson(QJsonDocument::Compact).size() > MaxExtensionBytes)
            return fail(error, QStringLiteral("Workbook extensions exceed %1 bytes").arg(MaxExtensionBytes));
        result->extensions = extensions.toObject();
    }
    return true;
}
} // namespace

std::optional<DecodedWorkbook> decodeWorkbook(const QByteArray &bytes, QString *error)
{
    if (bytes.size() > Workbook::MaxFileBytes) {
        fail(error, QStringLiteral("Workbook is larger than %1 bytes").arg(Workbook::MaxFileBytes));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(error, QStringLiteral("Invalid workbook JSON at byte %1: %2")
                        .arg(parseError.offset).arg(parseError.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    QString unknown;
    qint64 version = 0, next = 0;
    if (root.value(QStringLiteral("format")).toString() != QLatin1StringView(WorkbookFormatName)) {
        fail(error, QStringLiteral("Not a QindaTK workbook"));
        return std::nullopt;
    }
    if (!integerValue(root.value(QStringLiteral("version")), 0, 1 << 30, &version)
        || version < 1 || version > WorkbookFormatVersion) {
        fail(error, QStringLiteral("Unsupported workbook version"));
        return std::nullopt;
    }
    const bool v2 = version >= 2;
    if (!(v2 ? onlyKeys(root, {"format", "version", "documentId", "nextSheetNumber", "sheets", "names", "revision",
                               "resetGeneration", "extensions"}, &unknown)
             : onlyKeys(root, {"format", "version", "documentId", "nextSheetNumber", "sheets"}, &unknown))) {
        fail(error, QStringLiteral("Unsupported workbook field '%1'").arg(unknown));
        return std::nullopt;
    }
    DecodedWorkbook result;
    result.documentId = root.value(QStringLiteral("documentId")).toString();
    if (QUuid(result.documentId).isNull()
        || QUuid(result.documentId).toString(QUuid::WithoutBraces) != result.documentId) {
        fail(error, QStringLiteral("Workbook documentId must be a canonical UUID"));
        return std::nullopt;
    }
    if (!integerValue(root.value(QStringLiteral("nextSheetNumber")), 1, qint64(1) << 53, &next)) {
        fail(error, QStringLiteral("Workbook nextSheetNumber must be a positive integer"));
        return std::nullopt;
    }
    result.nextSheetNumber = quint64(next);
    const QJsonArray sheets = root.value(QStringLiteral("sheets")).toArray();
    if (!root.value(QStringLiteral("sheets")).isArray() || sheets.isEmpty() || sheets.size() > Workbook::MaxSheets) {
        fail(error, QStringLiteral("A workbook has 1 to %1 sheets").arg(Workbook::MaxSheets));
        return std::nullopt;
    }
    QHash<QString, QString> idsByFoldedName;
    QSet<QString> ids;
    std::vector<QJsonArray> cellArrays;
    std::vector<QJsonObject> sheetObjects;
    for (const QJsonValue &value : sheets) {
        const QJsonObject object = value.toObject();
        if (!value.isObject() || !onlyKeys(object, {"id", "name", "rows", "columns", "cells"}, &unknown, v2)) {
            fail(error, QStringLiteral("Invalid sheet entry %1").arg(unknown));
            return std::nullopt;
        }
        DecodedSheet sheet;
        sheet.id = object.value(QStringLiteral("id")).toString();
        sheet.name = object.value(QStringLiteral("name")).toString();
        qint64 rows = 0, columns = 0;
        const bool badId = sheet.id.isEmpty() || sheet.id.size() > MaxSheetIdLength
            || std::any_of(sheet.id.cbegin(), sheet.id.cend(), [](QChar c) { return c.unicode() < 0x20; });
        if (badId || ids.contains(sheet.id)) {
            fail(error, QStringLiteral("Sheet IDs must be unique, nonempty text"));
            return std::nullopt;
        }
        if (const QString problem = sheetNameProblem(sheet.name); !problem.isEmpty()) {
            fail(error, QStringLiteral("Sheet '%1': %2").arg(sheet.name, problem));
            return std::nullopt;
        }
        if (idsByFoldedName.contains(sheet.name.toCaseFolded())) {
            fail(error, QStringLiteral("Duplicate sheet name '%1'").arg(sheet.name));
            return std::nullopt;
        }
        if (!integerValue(object.value(QStringLiteral("rows")), 1, MaxRows, &rows)
            || !integerValue(object.value(QStringLiteral("columns")), 1, MaxColumns, &columns)
            || !object.value(QStringLiteral("cells")).isArray()) {
            fail(error, QStringLiteral("Sheet '%1' has invalid dimensions or cells").arg(sheet.name));
            return std::nullopt;
        }
        sheet.rows = int(rows);
        sheet.columns = int(columns);
        ids.insert(sheet.id);
        idsByFoldedName.insert(sheet.name.toCaseFolded(), sheet.id);
        cellArrays.push_back(object.value(QStringLiteral("cells")).toArray());
        sheetObjects.push_back(object);
        result.sheets.push_back(std::move(sheet));
    }
    const SheetIdLookup lookup = [&](const QString &name) { return idsByFoldedName.value(name.toCaseFolded()); };
    if (v2 && !decodeSessionHeader(root, &result, error)) return std::nullopt;
    if (v2) {
        QHash<QString, QPoint> bounds;
        for (const DecodedSheet &sheet : result.sheets) bounds.insert(sheet.id, QPoint(sheet.columns, sheet.rows));
        QString problem;
        if (!decodeNames(root.value(QStringLiteral("names")), lookup,
                         [&](const QString &id) { return bounds.value(id); }, &result.names, &problem)) {
            fail(error, problem);
            return std::nullopt;
        }
        for (size_t i = 0; i < result.sheets.size(); ++i) {
            DecodedSheet &sheet = result.sheets[i];
            RuleLookups rules;
            rules.sheetIdForName = lookup;
            rules.names = [&](const QString &name) {
                const NamedRange *entry = findName(*result.names, name);
                return entry ? entry->name : QString();
            };
            rules.rows = sheet.rows;
            rules.columns = sheet.columns;
            if (!decodeSheetFeatures(sheetObjects[i], rules, &sheet.features, &problem)) {
                fail(error, QStringLiteral("Sheet '%1': %2").arg(sheet.name, problem));
                return std::nullopt;
            }
        }
    }
    for (size_t i = 0; i < result.sheets.size(); ++i) {
        DecodedSheet &sheet = result.sheets[i];
        QSet<quint64> seen;
        for (const QJsonValue &item : cellArrays[i]) {
            const QJsonArray cell = item.toArray();
            RefPoint point;
            const QString address = cell.size() == 3 ? cell.at(0).toString() : QString();
            if (!item.isArray() || cell.size() != 3 || !parseCellAddress(address, &point)
                || point.rowAbsolute || point.columnAbsolute || point.row >= sheet.rows
                || point.column >= sheet.columns || seen.contains(positionKey(point.row, point.column))) {
                fail(error, QStringLiteral("Sheet '%1' has an invalid or duplicate cell address '%2'")
                                .arg(sheet.name, address));
                return std::nullopt;
            }
            seen.insert(positionKey(point.row, point.column));
            const QString type = cell.at(1).toString();
            const QJsonValue value = cell.at(2);
            CellContent content;
            content.kind = CellContent::Kind::Literal;
            bool ok = true;
            if (type == u"n") {
                ok = value.isDouble() && std::isfinite(value.toDouble());
                content.literal = Value::fromNumber(value.toDouble());
            } else if (type == u"s") {
                ok = value.isString() && !value.toString().isEmpty()
                    && value.toString().size() <= Workbook::MaxTextLength;
                content.literal = Value::fromText(value.toString());
            } else if (type == u"b") {
                ok = value.isBool();
                content.literal = Value::fromBool(value.toBool());
            } else if (type == u"e") {
                const ErrorKind kind = errorFromText(value.toString());
                ok = value.isString() && kind != ErrorKind::None && errorText(kind) == value.toString();
                content.literal = Value::fromError(kind);
            } else if (type == u"f") {
                const ParseResult parsed = parseFormula(value.toString(), lookup, StoredFormulaLength);
                if (!parsed.ok()) {
                    fail(error, QStringLiteral("Sheet '%1' cell %2: %3 (characters %4-%5)")
                                    .arg(sheet.name, address, parsed.error.message)
                                    .arg(parsed.error.start).arg(parsed.error.end));
                    return std::nullopt;
                }
                content.kind = CellContent::Kind::Formula;
                content.formula = parsed.formula;
                ++result.formulaCount;
            } else {
                ok = false;
            }
            if (!ok) {
                fail(error, QStringLiteral("Sheet '%1' cell %2 has an invalid value").arg(sheet.name, address));
                return std::nullopt;
            }
            sheet.cells.push_back({point.row, point.column, std::move(content)});
        }
        std::sort(sheet.cells.begin(), sheet.cells.end(), [](const CellEntry &a, const CellEntry &b) {
            return positionKey(a.row, a.column) < positionKey(b.row, b.column);
        });
        result.cellCount += qint64(sheet.cells.size());
    }
    return result;
}

QByteArray encodeWorkbook(const DecodedWorkbook &workbook)
{
    QHash<QString, QString> names;
    for (const DecodedSheet &sheet : workbook.sheets) names.insert(sheet.id, sheet.name);
    const SheetNameLookup lookup = [&](const QString &id) { return names.value(id); };
    QJsonArray sheets;
    bool v2 = !workbook.names->empty();
    for (const DecodedSheet &sheet : workbook.sheets) {
        QJsonArray cells;
        for (const CellEntry &entry : sheet.cells) cells.append(encodeCell(entry, lookup));
        QJsonObject object = encodeSheetFeatures(*sheet.features, lookup);
        v2 = v2 || !object.isEmpty();
        object.insert(QStringLiteral("id"), sheet.id);
        object.insert(QStringLiteral("name"), sheet.name);
        object.insert(QStringLiteral("rows"), sheet.rows);
        object.insert(QStringLiteral("columns"), sheet.columns);
        object.insert(QStringLiteral("cells"), cells);
        sheets.append(object);
    }
    // AGENT-NOTE: a workbook without names, sheet features or a session
    // header is written as version 1, so files from before version 2 re-save
    // byte-identically.
    v2 = v2 || workbook.hasHeader;
    QJsonObject root{{QStringLiteral("format"), QLatin1StringView(WorkbookFormatName)},
                     {QStringLiteral("version"), v2 ? WorkbookFormatVersion : 1},
                     {QStringLiteral("documentId"), workbook.documentId},
                     {QStringLiteral("nextSheetNumber"), qint64(workbook.nextSheetNumber)},
                     {QStringLiteral("sheets"), sheets}};
    if (!workbook.names->empty()) root.insert(QStringLiteral("names"), encodeNames(*workbook.names, lookup));
    if (workbook.hasHeader) {
        root.insert(QStringLiteral("revision"), QString::number(workbook.revision));
        root.insert(QStringLiteral("resetGeneration"), QString::number(workbook.resetGeneration));
        if (!workbook.extensions.isEmpty()) root.insert(QStringLiteral("extensions"), workbook.extensions);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace QindaTK::Calc
