// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_lexer_p.h"

#include <algorithm>

namespace QindaTK::Calc {
namespace {
bool fail(ParseError *error, const QString &message, qint32 start, qint32 end)
{
    *error = {message, start, std::max(end, start + 1)};
    return false;
}
bool isWordChar(QChar c)
{ return isFormulaLetter(c) || isFormulaDigit(c) || c == u'_' || c == u'.' || c == u'$'; }
bool isDigitChar(QChar c) { return isFormulaDigit(c); }
bool isLetter(QChar c) { return isFormulaLetter(c); }
} // namespace

bool lexFormula(const QString &text, std::vector<Token> *tokens, ParseError *error)
{
    const qint32 n = qint32(text.size());
    qint32 i = 1; // skip '='
    while (i < n) {
        const QChar c = text[i];
        if (c.isSpace()) { ++i; continue; }
        Token token;
        token.start = i;
        if (isDigitChar(c) || (c == u'.' && i + 1 < n && isDigitChar(text[i + 1]))) {
            qint32 j = i;
            while (j < n && isDigitChar(text[j])) ++j;
            if (j < n && text[j] == u'.') { ++j; while (j < n && isDigitChar(text[j])) ++j; }
            if (j < n && (text[j] == u'e' || text[j] == u'E')) {
                qint32 k = j + 1;
                if (k < n && (text[k] == u'+' || text[k] == u'-')) ++k;
                if (k >= n || !isDigitChar(text[k]))
                    return fail(error, QStringLiteral("Number exponent needs digits"), i, k);
                while (k < n && isDigitChar(text[k])) ++k;
                j = k;
            }
            if (!parseNumber(QStringView(text).mid(i, j - i), &token.number))
                return fail(error, QStringLiteral("Number is out of range"), i, j);
            token.kind = Token::Kind::Number;
            i = j;
        } else if (c == u'"' || c == u'\'') {
            QString body;
            qint32 j = i + 1;
            bool closed = false;
            while (j < n) {
                if (text[j] == c) {
                    if (j + 1 < n && text[j + 1] == c) { body += c; j += 2; continue; }
                    closed = true; ++j; break;
                }
                body += text[j++];
            }
            if (!closed)
                return fail(error, c == u'"' ? QStringLiteral("Text is missing its closing quote")
                                      : QStringLiteral("Sheet name is missing its closing quote"), i, n);
            token.kind = c == u'"' ? Token::Kind::String : Token::Kind::Quoted;
            token.text = body;
            i = j;
        } else if (c == u'#') {
            ErrorKind found = ErrorKind::None;
            qint32 length = 0;
            for (qint32 len = std::min<qint32>(8, n - i); len >= 4 && found == ErrorKind::None; --len) {
                found = errorFromText(QStringView(text).mid(i, len));
                length = len;
            }
            if (found == ErrorKind::None)
                return fail(error, QStringLiteral("Unknown error value"), i, i + 1);
            token.kind = Token::Kind::Error;
            token.error = found;
            i += length;
        } else if (isLetter(c) || c == u'_' || c == u'$') {
            qint32 j = i;
            while (j < n && isWordChar(text[j])) ++j;
            token.kind = Token::Kind::Word;
            token.text = text.mid(i, j - i);
            i = j;
        } else {
            static const char *twoChar[] = {"<>", "<=", ">="};
            QString spelling(c);
            for (const char *op : twoChar)
                if (i + 1 < n && text[i] == QLatin1Char(op[0]) && text[i + 1] == QLatin1Char(op[1]))
                    spelling = QString::fromLatin1(op, 2);
            if (spelling == u"(") token.kind = Token::Kind::LParen;
            else if (spelling == u")") token.kind = Token::Kind::RParen;
            else if (spelling == u",") token.kind = Token::Kind::Comma;
            else if (spelling == u":") token.kind = Token::Kind::Colon;
            else if (spelling == u"!") token.kind = Token::Kind::Bang;
            else if (spelling == u"%") token.kind = Token::Kind::Percent;
            else if (QStringLiteral("+-*/^&=<>").contains(spelling) || spelling.size() == 2)
                token.kind = Token::Kind::Operator;
            else
                return fail(error, QStringLiteral("Unexpected character '%1'").arg(spelling), i, i + 1);
            token.text = spelling;
            i += qint32(spelling.size());
        }
        token.end = i;
        tokens->push_back(std::move(token));
    }
    Token end;
    end.start = end.end = n;
    tokens->push_back(end);
    return true;
}

} // namespace QindaTK::Calc
