// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_value.h"

#include <QLocale>
#include <cmath>

namespace QindaTK::Calc {
namespace {
struct ErrorSpelling { ErrorKind kind; const char *text; };
constexpr ErrorSpelling errorSpellings[] = {
    {ErrorKind::Div0, "#DIV/0!"}, {ErrorKind::Value, "#VALUE!"}, {ErrorKind::Ref, "#REF!"},
    {ErrorKind::Name, "#NAME?"}, {ErrorKind::Num, "#NUM!"}, {ErrorKind::NA, "#N/A"},
    {ErrorKind::Cycle, "#CYCLE!"},
};
bool isDigit(QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); }
QString upperExponent(QString text)
{
    const qsizetype e = text.indexOf(QLatin1Char('e'));
    if (e >= 0) text[e] = QLatin1Char('E');
    return text;
}
int typeRank(Value::Type type)
{
    switch (type) {
    case Value::Type::Number: return 0;
    case Value::Type::Text: return 1;
    case Value::Type::Boolean: return 2;
    default: return 3;
    }
}
Value zeroLike(Value::Type type)
{
    switch (type) {
    case Value::Type::Text: return Value::fromText(QString());
    case Value::Type::Boolean: return Value::fromBool(false);
    default: return Value::fromNumber(0.0);
    }
}
} // namespace

Value Value::fromNumber(double value)
{
    if (!std::isfinite(value)) return fromError(ErrorKind::Num);
    Value result;
    result.type = Type::Number;
    result.number = value == 0.0 ? 0.0 : value; // fold -0 into 0
    return result;
}
Value Value::fromText(const QString &value)
{
    Value result;
    result.type = Type::Text;
    result.text = value;
    return result;
}
Value Value::fromBool(bool value)
{
    Value result;
    result.type = Type::Boolean;
    result.boolean = value;
    return result;
}
Value Value::fromError(ErrorKind kind)
{
    Value result;
    result.type = Type::Error;
    result.error = kind == ErrorKind::None ? ErrorKind::Value : kind;
    return result;
}
bool operator==(const Value &a, const Value &b)
{
    if (a.type != b.type) return false;
    switch (a.type) {
    case Value::Type::Blank: return true;
    case Value::Type::Number: return a.number == b.number;
    case Value::Type::Text: return a.text == b.text;
    case Value::Type::Boolean: return a.boolean == b.boolean;
    case Value::Type::Error: return a.error == b.error;
    }
    return false;
}

QString errorText(ErrorKind kind)
{
    for (const auto &entry : errorSpellings)
        if (entry.kind == kind) return QString::fromLatin1(entry.text);
    return {};
}
ErrorKind errorFromText(QStringView text)
{
    for (const auto &entry : errorSpellings)
        if (text.compare(QLatin1StringView(entry.text), Qt::CaseInsensitive) == 0) return entry.kind;
    return ErrorKind::None;
}
QString typeName(Value::Type type)
{
    switch (type) {
    case Value::Type::Blank: return QStringLiteral("blank");
    case Value::Type::Number: return QStringLiteral("number");
    case Value::Type::Text: return QStringLiteral("text");
    case Value::Type::Boolean: return QStringLiteral("boolean");
    case Value::Type::Error: return QStringLiteral("error");
    }
    return {};
}

QString formatNumber(double value)
{
    if (value == 0.0) return QStringLiteral("0");
    return upperExponent(QString::number(value, 'g', 15));
}
QString numberLiteral(double value)
{
    if (value == 0.0) return QStringLiteral("0");
    return upperExponent(QString::number(value, 'g', QLocale::FloatingPointShortest));
}
QString displayText(const Value &value)
{
    switch (value.type) {
    case Value::Type::Blank: return {};
    case Value::Type::Number: return formatNumber(value.number);
    case Value::Type::Text: return value.text;
    case Value::Type::Boolean: return value.boolean ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    case Value::Type::Error: return errorText(value.error);
    }
    return {};
}
bool parseNumber(QStringView text, double *value)
{
    qsizetype i = 0;
    const qsizetype n = text.size();
    if (i < n && (text[i] == QLatin1Char('+') || text[i] == QLatin1Char('-'))) ++i;
    qsizetype digits = 0;
    while (i < n && isDigit(text[i])) { ++i; ++digits; }
    if (i < n && text[i] == QLatin1Char('.')) {
        ++i;
        while (i < n && isDigit(text[i])) { ++i; ++digits; }
    }
    if (digits == 0) return false;
    if (i < n && (text[i] == QLatin1Char('e') || text[i] == QLatin1Char('E'))) {
        ++i;
        if (i < n && (text[i] == QLatin1Char('+') || text[i] == QLatin1Char('-'))) ++i;
        qsizetype exponentDigits = 0;
        while (i < n && isDigit(text[i])) { ++i; ++exponentDigits; }
        if (exponentDigits == 0) return false;
    }
    if (i != n) return false;
    bool ok = false;
    const double parsed = QLocale::c().toDouble(text, &ok);
    if (!ok || !std::isfinite(parsed)) return false;
    if (value) *value = parsed == 0.0 ? 0.0 : parsed;
    return true;
}

QVariant toVariant(const Value &value)
{
    switch (value.type) {
    case Value::Type::Blank: return {};
    case Value::Type::Number: return value.number;
    case Value::Type::Text: return value.text;
    case Value::Type::Boolean: return value.boolean;
    case Value::Type::Error: return errorText(value.error);
    }
    return {};
}

NumberResult toNumber(const Value &value)
{
    switch (value.type) {
    case Value::Type::Blank: return {0.0, ErrorKind::None};
    case Value::Type::Number: return {value.number, ErrorKind::None};
    case Value::Type::Boolean: return {value.boolean ? 1.0 : 0.0, ErrorKind::None};
    case Value::Type::Error: return {0.0, value.error};
    case Value::Type::Text: {
        double parsed = 0.0;
        if (parseNumber(value.text, &parsed)) return {parsed, ErrorKind::None};
        return {0.0, ErrorKind::Value};
    }
    }
    return {0.0, ErrorKind::Value};
}
BoolResult toBool(const Value &value)
{
    switch (value.type) {
    case Value::Type::Blank: return {false, ErrorKind::None};
    case Value::Type::Number: return {value.number != 0.0, ErrorKind::None};
    case Value::Type::Boolean: return {value.boolean, ErrorKind::None};
    case Value::Type::Error: return {false, value.error};
    case Value::Type::Text:
        if (value.text.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0)
            return {true, ErrorKind::None};
        if (value.text.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0)
            return {false, ErrorKind::None};
        return {false, ErrorKind::Value};
    }
    return {false, ErrorKind::Value};
}
QString toText(const Value &value)
{
    return value.isBlank() ? QString() : displayText(value);
}
int compareValues(const Value &left, const Value &right)
{
    if (left.isBlank() && right.isBlank()) return 0;
    const Value a = left.isBlank() ? zeroLike(right.type) : left;
    const Value b = right.isBlank() ? zeroLike(left.type) : right;
    const int ra = typeRank(a.type), rb = typeRank(b.type);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (a.type) {
    case Value::Type::Number:
        return a.number < b.number ? -1 : (a.number > b.number ? 1 : 0);
    case Value::Type::Text: {
        const int c = a.text.compare(b.text, Qt::CaseInsensitive);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    case Value::Type::Boolean:
        return a.boolean == b.boolean ? 0 : (a.boolean ? 1 : -1);
    default:
        return 0;
    }
}

} // namespace QindaTK::Calc
