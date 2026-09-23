// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_codec_p.h"

#include <QJsonArray>
#include <QJsonObject>
#include <cmath>

// Version 2 of the native file: sheet features and workbook names
// (docs/workbook.md#native-file-format). Rules use the same keys as the QML
// API in compact form; sizes are keyed by column letters / row numbers.
namespace QindaTK::Calc {
namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
QJsonObject sizesToJson(const std::map<qint32, double> &sizes, bool rows)
{
    QJsonObject object;
    for (const auto &[index, size] : sizes)
        object.insert(rows ? QString::number(index + 1) : columnName(index), size);
    return object;
}
bool sizesFromJson(const QJsonValue &value, bool rows, int limit, std::map<qint32, double> *sizes, QString *error)
{
    if (value.isUndefined()) return true;
    if (!value.isObject()) return fail(error, QStringLiteral("Sizes must be an object"));
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool ok = false;
        const int index = rows ? it.key().toInt(&ok) - 1 : columnFromName(it.key());
        const double size = it.value().toDouble(-1);
        if ((rows && !ok) || index < 0 || index >= limit || !std::isfinite(size) || size < 1 || size > 100000)
            return fail(error, QStringLiteral("Invalid size entry '%1'").arg(it.key()));
        sizes->emplace(index, size);
    }
    return true;
}
} // namespace

QJsonObject encodeSheetFeatures(const SheetFeatures &features, const SheetNameLookup &names)
{
    QJsonObject object;
    if (!features.formats.empty()) {
        QJsonArray formats;
        for (const FormatLayer &layer : features.formats)
            formats.append(QJsonObject{{QStringLiteral("range"), areaText(layer.area)},
                                       {QStringLiteral("style"), QJsonObject::fromVariantMap(styleToMap(layer.style))}});
        object.insert(QStringLiteral("formats"), formats);
    }
    if (!features.conditional.empty()) {
        QJsonArray rules;
        for (const ConditionalRule &rule : features.conditional)
            rules.append(QJsonObject::fromVariantMap(conditionalToMap(rule, names, true)));
        object.insert(QStringLiteral("conditionalFormats"), rules);
    }
    if (!features.validations.empty()) {
        QJsonArray rules;
        for (const ValidationRule &rule : features.validations)
            rules.append(QJsonObject::fromVariantMap(validationToMap(rule, names, true)));
        object.insert(QStringLiteral("validations"), rules);
    }
    if (!features.merges.empty()) {
        QJsonArray merges;
        for (const Area &area : features.merges) merges.append(areaText(area));
        object.insert(QStringLiteral("merges"), merges);
    }
    const SheetProperties &p = features.properties;
    if (!p.tabColor.isEmpty()) object.insert(QStringLiteral("tabColor"), p.tabColor);
    if (p.hidden) object.insert(QStringLiteral("hidden"), true);
    if (p.protectedSheet) object.insert(QStringLiteral("protected"), true);
    if (p.frozenRows > 0) object.insert(QStringLiteral("frozenRows"), p.frozenRows);
    if (p.frozenColumns > 0) object.insert(QStringLiteral("frozenColumns"), p.frozenColumns);
    if (!p.columnWidths.empty()) object.insert(QStringLiteral("columnWidths"), sizesToJson(p.columnWidths, false));
    if (!p.rowHeights.empty()) object.insert(QStringLiteral("rowHeights"), sizesToJson(p.rowHeights, true));
    return object;
}

bool decodeSheetFeatures(const QJsonObject &sheet, const RuleLookups &lookups, FeaturesPtr *out, QString *error)
{
    auto features = std::make_shared<SheetFeatures>();
    const QJsonArray formats = sheet.value(QStringLiteral("formats")).toArray();
    if (formats.size() > MaxFormatLayers) return fail(error, QStringLiteral("Too many format areas"));
    for (const QJsonValue &item : formats) {
        const QJsonObject object = item.toObject();
        FormatLayer layer;
        if (!parseArea(object.value(QStringLiteral("range")).toString(), &layer.area)
            || layer.area.lastRow >= lookups.rows || layer.area.lastColumn >= lookups.columns)
            return fail(error, QStringLiteral("Invalid format range '%1'").arg(object.value(QStringLiteral("range")).toString()));
        if (object.size() != 2 || !styleFromMap(object.value(QStringLiteral("style")).toObject().toVariantMap(), &layer.style, error))
            return fail(error, QStringLiteral("Invalid format at %1: %2").arg(areaText(layer.area), error ? *error : QString()));
        features->formats.push_back(std::move(layer));
    }
    QSet<QString> ids;
    const QJsonArray conditional = sheet.value(QStringLiteral("conditionalFormats")).toArray();
    if (conditional.size() > MaxSheetRules) return fail(error, QStringLiteral("Too many conditional formats"));
    for (const QJsonValue &item : conditional) {
        ConditionalRule rule;
        if (!conditionalFromMap(item.toObject().toVariantMap(), lookups, &rule, error)) return false;
        if (rule.id.isEmpty() || ids.contains(rule.id)) return fail(error, QStringLiteral("Conditional format ids must be unique"));
        ids.insert(rule.id);
        features->conditional.push_back(std::move(rule));
    }
    ids.clear();
    const QJsonArray validations = sheet.value(QStringLiteral("validations")).toArray();
    if (validations.size() > MaxSheetRules) return fail(error, QStringLiteral("Too many validation rules"));
    for (const QJsonValue &item : validations) {
        ValidationRule rule;
        if (!validationFromMap(item.toObject().toVariantMap(), lookups, &rule, error)) return false;
        if (rule.id.isEmpty() || ids.contains(rule.id)) return fail(error, QStringLiteral("Validation rule ids must be unique"));
        for (const ValidationRule &other : features->validations)
            for (const Area &a : other.areas)
                for (const Area &b : rule.areas)
                    if (a.intersects(b)) return fail(error, QStringLiteral("Validation rules %1 and %2 overlap").arg(other.id, rule.id));
        ids.insert(rule.id);
        features->validations.push_back(std::move(rule));
    }
    const QJsonArray merges = sheet.value(QStringLiteral("merges")).toArray();
    if (merges.size() > MaxMergedAreas) return fail(error, QStringLiteral("Too many merged areas"));
    for (const QJsonValue &item : merges) {
        Area area;
        if (!parseArea(item.toString(), &area) || area.cellCount() < 2 || area.lastRow >= lookups.rows
            || area.lastColumn >= lookups.columns)
            return fail(error, QStringLiteral("Invalid merged area '%1'").arg(item.toString()));
        for (const Area &other : features->merges)
            if (other.intersects(area)) return fail(error, QStringLiteral("Merged areas %1 and %2 overlap").arg(areaText(other), areaText(area)));
        features->merges.push_back(area);
    }
    SheetProperties &p = features->properties;
    const QJsonValue tab = sheet.value(QStringLiteral("tabColor"));
    if (!tab.isUndefined() && (p.tabColor = normalizedColor(tab.toString())).isEmpty())
        return fail(error, QStringLiteral("tabColor must be #rrggbb"));
    for (const auto &[key, target] : {std::pair{"hidden", &p.hidden}, std::pair{"protected", &p.protectedSheet}}) {
        const QJsonValue value = sheet.value(QLatin1StringView(key));
        if (value.isUndefined()) continue;
        if (!value.isBool()) return fail(error, QStringLiteral("'%1' must be true or false").arg(QLatin1StringView(key)));
        *target = value.toBool();
    }
    for (const auto &[key, target, limit] : {std::tuple{"frozenRows", &p.frozenRows, lookups.rows},
                                             std::tuple{"frozenColumns", &p.frozenColumns, lookups.columns}}) {
        const QJsonValue value = sheet.value(QLatin1StringView(key));
        if (value.isUndefined()) continue;
        const double number = value.toDouble(-1);
        if (number != std::floor(number) || number < 0 || number >= limit)
            return fail(error, QStringLiteral("'%1' is out of range").arg(QLatin1StringView(key)));
        *target = qint32(number);
    }
    if (!sizesFromJson(sheet.value(QStringLiteral("columnWidths")), false, lookups.columns, &p.columnWidths, error)
        || !sizesFromJson(sheet.value(QStringLiteral("rowHeights")), true, lookups.rows, &p.rowHeights, error)) return false;
    *out = features->isEmpty() ? emptyFeatures() : FeaturesPtr(features);
    return true;
}

QJsonArray encodeNames(const NameTable &names, const SheetNameLookup &sheetNames)
{
    QJsonArray array;
    for (const NamedRange &entry : names)
        array.append(QJsonObject{{QStringLiteral("name"), entry.name},
                                 {QStringLiteral("ref"), entry.sheetId.isEmpty() ? QStringLiteral("#REF!")
                                  : quoteSheetName(sheetNames(entry.sheetId)) + QLatin1Char('!') + areaText(entry.area)}});
    return array;
}

bool decodeNames(const QJsonValue &value, const SheetIdLookup &sheetIds, const std::function<QPoint(const QString &)> &bounds,
                 NamesPtr *out, QString *error)
{
    auto names = std::make_shared<NameTable>();
    if (value.isUndefined()) { *out = names; return true; }
    if (!value.isArray() || value.toArray().size() > MaxNames) return fail(error, QStringLiteral("Invalid names"));
    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject object = item.toObject();
        NamedRange entry;
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.key = entry.name.toCaseFolded();
        const QString ref = object.value(QStringLiteral("ref")).toString();
        if (object.size() != 2 || !nameProblem(entry.name).isEmpty())
            return fail(error, QStringLiteral("Invalid name '%1'").arg(entry.name));
        if (ref != u"#REF!") {
            const qsizetype bang = ref.lastIndexOf(QLatin1Char('!'));
            QString sheet = bang > 0 ? ref.left(bang) : QString();
            if (sheet.size() >= 2 && sheet.startsWith(QLatin1Char('\'')) && sheet.endsWith(QLatin1Char('\'')))
                sheet = sheet.mid(1, sheet.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
            entry.sheetId = sheetIds(sheet);
            const QPoint limit = entry.sheetId.isEmpty() ? QPoint() : bounds(entry.sheetId);
            if (entry.sheetId.isEmpty() || !parseArea(QStringView(ref).mid(bang + 1), &entry.area)
                || entry.area.lastRow >= limit.y() || entry.area.lastColumn >= limit.x())
                return fail(error, QStringLiteral("Name '%1' has an invalid reference '%2'").arg(entry.name, ref));
        }
        if (findName(*names, entry.name)) return fail(error, QStringLiteral("Duplicate name '%1'").arg(entry.name));
        const auto at = std::lower_bound(names->begin(), names->end(), entry.key,
                                         [](const NamedRange &e, const QString &k) { return e.key < k; });
        names->insert(at, std::move(entry));
    }
    *out = names;
    return true;
}

} // namespace QindaTK::Calc
