// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Formula tokens shared by the lexer and parser. Not installed.

#include "workbook_formula.h"

namespace QindaTK::Calc {

struct Token {
    enum class Kind { End, Number, String, Error, Word, Quoted, Operator, LParen, RParen, Comma, Colon, Bang, Percent };
    Kind kind = Kind::End;
    qint32 start = 0, end = 0;
    double number = 0.0;
    ErrorKind error = ErrorKind::None;
    QString text;  // word, string body, quoted sheet name or operator spelling
};

inline bool isFormulaLetter(QChar c) { return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'); }
inline bool isFormulaDigit(QChar c) { return c >= u'0' && c <= u'9'; }
// Splits `text` (starting with '=') into tokens followed by an End token.
bool lexFormula(const QString &text, std::vector<Token> *tokens, ParseError *error);

} // namespace QindaTK::Calc
