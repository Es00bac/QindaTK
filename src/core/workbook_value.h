// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringView>
#include <QVariant>

// Typed cell values for the workbook calculation engine (docs/workbook.md).
// Values are locale independent: numbers use '.' and never carry a format.
namespace QindaTK::Calc {

// AGENT-CONTRACT: errorText()/errorFromText() spell these exactly as
// documented in docs/workbook.md; the native file format stores the text.
enum class ErrorKind : quint8 { None, Div0, Value, Ref, Name, Num, NA, Cycle };

struct Value {
    enum class Type : quint8 { Blank, Number, Text, Boolean, Error };
    Type type = Type::Blank;
    ErrorKind error = ErrorKind::None;
    bool boolean = false;
    double number = 0.0;
    QString text;

    static Value blank() { return {}; }
    // A non-finite number is never stored; it becomes #NUM!.
    static Value fromNumber(double value);
    static Value fromText(const QString &value);
    static Value fromBool(bool value);
    static Value fromError(ErrorKind kind);
    bool isBlank() const { return type == Type::Blank; }
    bool isNumber() const { return type == Type::Number; }
    bool isText() const { return type == Type::Text; }
    bool isBoolean() const { return type == Type::Boolean; }
    bool isError() const { return type == Type::Error; }
    friend bool operator==(const Value &a, const Value &b);
};

QString errorText(ErrorKind kind);
// Recognizes the documented literals case-insensitively; None otherwise.
ErrorKind errorFromText(QStringView text);
QString typeName(Value::Type type);

// Display text for a cell ("General"): numbers with at most 15 significant
// digits and an upper-case exponent, TRUE/FALSE, error literals, raw text.
QString formatNumber(double value);
QString displayText(const Value &value);
// Shortest text that parses back to exactly the same double.
QString numberLiteral(double value);
// Strict C-locale decimal grammar over the whole text:
// [+-]? (digits [. digits?] | . digits) ([eE] [+-]? digits)?; finite only.
bool parseNumber(QStringView text, double *value);

// QML projection: number -> double, text -> string, boolean -> bool,
// error -> its literal string, blank -> undefined (invalid QVariant).
QVariant toVariant(const Value &value);

// Coercions used by operators and functions. An error input propagates.
struct NumberResult { double number = 0.0; ErrorKind error = ErrorKind::None; };
struct BoolResult { bool value = false; ErrorKind error = ErrorKind::None; };
NumberResult toNumber(const Value &value);
BoolResult toBool(const Value &value);
QString toText(const Value &value);
// Spreadsheet ordering for comparison operators: blank adopts the other
// operand's type; otherwise Number < Text < Boolean; text compares
// case-insensitively. Caller handles errors first.
int compareValues(const Value &a, const Value &b);

} // namespace QindaTK::Calc
