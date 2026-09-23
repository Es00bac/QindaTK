// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "workbook_value.h"

// Cell number formats (docs/workbook.md#number-formats). Pure and
// thread-safe: the native and CSV providers format off the GUI thread.
namespace QindaTK::Calc {

inline constexpr qsizetype MaxNumberFormatLength = 64;

// "" when `format` is supported. Supported codes: General (or ""), "@"
// (text), numeric patterns — optional '#,##' grouping, '0' integer digits,
// '.' and up to 15 decimal '0's, then '%' or 'E+00' — and date/time
// patterns of y, m, d, h, s and AM/PM over serial days since 1899-12-30
// ("m" next to h or s means minutes). Either kind may carry a literal
// prefix/suffix of quoted text or $ € £ ¥ - + ( ) / : , . and spaces.
QString numberFormatProblem(const QString &format);
// Display text of `value` under `format`. Non-numbers, "General" and "@"
// use displayText(); an out-of-range date shows "######".
QString formatValue(const Value &value, const QString &format);
// Half away from zero after removing binary noise (ROUND and fixed formats).
double roundDecimal(double number, int digits);

} // namespace QindaTK::Calc
