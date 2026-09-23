// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_formula.h"

// The supported function list (docs/workbook.md#functions).
namespace QindaTK::Calc {
namespace {
constexpr FunctionSpec functionTable[] = {
    {Function::Sum, "SUM", 1, MaxFunctionArguments, "SUM(value1, [value2], ...)"},
    {Function::Average, "AVERAGE", 1, MaxFunctionArguments, "AVERAGE(value1, [value2], ...)"},
    {Function::Min, "MIN", 1, MaxFunctionArguments, "MIN(value1, [value2], ...)"},
    {Function::Max, "MAX", 1, MaxFunctionArguments, "MAX(value1, [value2], ...)"},
    {Function::Count, "COUNT", 1, MaxFunctionArguments, "COUNT(value1, [value2], ...)"},
    {Function::CountA, "COUNTA", 1, MaxFunctionArguments, "COUNTA(value1, [value2], ...)"},
    {Function::If, "IF", 2, 3, "IF(condition, value_if_true, [value_if_false])"},
    {Function::IfError, "IFERROR", 2, 2, "IFERROR(value, value_if_error)"},
    {Function::And, "AND", 1, MaxFunctionArguments, "AND(logical1, [logical2], ...)"},
    {Function::Or, "OR", 1, MaxFunctionArguments, "OR(logical1, [logical2], ...)"},
    {Function::Not, "NOT", 1, 1, "NOT(logical)"},
    {Function::Abs, "ABS", 1, 1, "ABS(number)"},
    {Function::Round, "ROUND", 2, 2, "ROUND(number, digits)"},
};
} // namespace

const FunctionSpec *functionByName(QStringView name)
{
    for (const auto &spec : functionTable)
        if (name.compare(QLatin1StringView(spec.name), Qt::CaseInsensitive) == 0) return &spec;
    return nullptr;
}
const FunctionSpec &functionSpec(Function id)
{
    for (const auto &spec : functionTable)
        if (spec.id == id) return spec;
    return functionTable[0];
}
QStringList functionNames()
{
    QStringList names;
    for (const auto &spec : functionTable) names.append(QString::fromLatin1(spec.name));
    return names;
}

} // namespace QindaTK::Calc
