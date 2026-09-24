// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Rule and style definitions as maps — the QML API and the native file use
// the same keys (docs/workbook.md#formats-rules-and-names). Pure and
// thread-safe; errors name the offending field. Not installed.

#include "workbook_features_p.h"
#include <QVariantMap>

namespace QindaTK::Calc {

struct RuleLookups {
    SheetIdLookup sheetIdForName;
    SheetNameLookup sheetNameForId;
    NameLookup names;
    int rows = MaxRows;        // bounds of the sheet the rule belongs to
    int columns = MaxColumns;
};

// "#rgb"/"#rrggbb" (any case) -> "#rrggbb"; "" for anything else.
QString normalizedColor(const QString &text);
bool styleFromMap(const QVariantMap &map, CellStyle *style, QString *error);
QVariantMap styleToMap(const CellStyle &style);
bool areasFromValue(const QVariant &value, const RuleLookups &lookups, std::vector<Area> *areas, QString *error);
QStringList areasToList(const std::vector<Area> &areas);
// Rule values: "=formula" or a literal typed like a cell input.
bool operandFromText(const QString &text, const RuleLookups &lookups, RuleOperand *operand, QString *error);
QString operandText(const RuleOperand &operand, const SheetNameLookup &names);

bool conditionalFromMap(const QVariantMap &map, const RuleLookups &lookups, ConditionalRule *rule, QString *error);
// `compact` omits defaults (the native file); the QML API lists every key.
QVariantMap conditionalToMap(const ConditionalRule &rule, const SheetNameLookup &names, bool compact);
bool validationFromMap(const QVariantMap &map, const RuleLookups &lookups, ValidationRule *rule, QString *error);
QVariantMap validationToMap(const ValidationRule &rule, const SheetNameLookup &names, bool compact);

QString operatorName(RuleOperator op);

} // namespace QindaTK::Calc
