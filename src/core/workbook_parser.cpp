// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_formula.h"

#include <algorithm>

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

struct Token {
    enum class Kind { End, Number, String, Error, Word, Quoted, Operator, LParen, RParen, Comma, Colon, Bang, Percent };
    Kind kind = Kind::End;
    qint32 start = 0, end = 0;
    double number = 0.0;
    ErrorKind error = ErrorKind::None;
    QString text;  // word, string body, quoted sheet name or operator spelling
};

bool isLetter(QChar c) { return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'); }
bool isDigitChar(QChar c) { return c >= u'0' && c <= u'9'; }
bool isWordChar(QChar c) { return isLetter(c) || isDigitChar(c) || c == u'_' || c == u'.' || c == u'$'; }

class Parser {
public:
    Parser(const QString &text, const SheetIdLookup &lookup, int maximumLength)
        : m_text(text), m_lookup(lookup), m_maximumLength(maximumLength) {}
    ParseResult run();
private:
    bool fail(const QString &message, qint32 start, qint32 end);
    bool lex();
    const Token &peek() const { return m_tokens[m_pos]; }
    Token take() { return m_tokens[m_pos++]; }
    bool isOperator(const char *spelling) const
    { return peek().kind == Token::Kind::Operator && peek().text == QLatin1StringView(spelling); }
    qint32 add(Node node, std::initializer_list<qint32> children)
    { return addWith(node, children.begin(), children.size()); }
    qint32 addWith(Node node, const qint32 *children, size_t count);
    qint32 comparison();
    qint32 binaryLevel(int level);
    qint32 percent();
    qint32 unary();
    qint32 primary();
    qint32 call(const Token &name);
    qint32 reference(qint32 sheet, qint32 start);
    qint32 sheetIndex(const QString &id);

    const QString &m_text;
    const SheetIdLookup &m_lookup;
    const int m_maximumLength;
    std::vector<Token> m_tokens;
    std::vector<qint32> m_heights;
    size_t m_pos = 0;
    int m_nesting = 0;
    Formula m_formula;
    ParseError m_error;
    bool m_failed = false;
};

bool Parser::fail(const QString &message, qint32 start, qint32 end)
{
    if (!m_failed) {
        m_failed = true;
        m_error = {message, start, std::max(end, start + 1)};
    }
    return false;
}

bool Parser::lex()
{
    const qint32 n = qint32(m_text.size());
    qint32 i = 1; // skip '='
    while (i < n) {
        const QChar c = m_text[i];
        if (c.isSpace()) { ++i; continue; }
        Token token;
        token.start = i;
        if (isDigitChar(c) || (c == u'.' && i + 1 < n && isDigitChar(m_text[i + 1]))) {
            qint32 j = i;
            while (j < n && isDigitChar(m_text[j])) ++j;
            if (j < n && m_text[j] == u'.') { ++j; while (j < n && isDigitChar(m_text[j])) ++j; }
            if (j < n && (m_text[j] == u'e' || m_text[j] == u'E')) {
                qint32 k = j + 1;
                if (k < n && (m_text[k] == u'+' || m_text[k] == u'-')) ++k;
                if (k >= n || !isDigitChar(m_text[k]))
                    return fail(QStringLiteral("Number exponent needs digits"), i, k);
                while (k < n && isDigitChar(m_text[k])) ++k;
                j = k;
            }
            if (!parseNumber(QStringView(m_text).mid(i, j - i), &token.number))
                return fail(QStringLiteral("Number is out of range"), i, j);
            token.kind = Token::Kind::Number;
            i = j;
        } else if (c == u'"' || c == u'\'') {
            QString body;
            qint32 j = i + 1;
            bool closed = false;
            while (j < n) {
                if (m_text[j] == c) {
                    if (j + 1 < n && m_text[j + 1] == c) { body += c; j += 2; continue; }
                    closed = true; ++j; break;
                }
                body += m_text[j++];
            }
            if (!closed)
                return fail(c == u'"' ? QStringLiteral("Text is missing its closing quote")
                                      : QStringLiteral("Sheet name is missing its closing quote"), i, n);
            token.kind = c == u'"' ? Token::Kind::String : Token::Kind::Quoted;
            token.text = body;
            i = j;
        } else if (c == u'#') {
            ErrorKind found = ErrorKind::None;
            qint32 length = 0;
            for (qint32 len = std::min<qint32>(8, n - i); len >= 4 && found == ErrorKind::None; --len) {
                found = errorFromText(QStringView(m_text).mid(i, len));
                length = len;
            }
            if (found == ErrorKind::None)
                return fail(QStringLiteral("Unknown error value"), i, i + 1);
            token.kind = Token::Kind::Error;
            token.error = found;
            i += length;
        } else if (isLetter(c) || c == u'_' || c == u'$') {
            qint32 j = i;
            while (j < n && isWordChar(m_text[j])) ++j;
            token.kind = Token::Kind::Word;
            token.text = m_text.mid(i, j - i);
            i = j;
        } else {
            static const char *twoChar[] = {"<>", "<=", ">="};
            QString spelling(c);
            for (const char *op : twoChar)
                if (i + 1 < n && m_text[i] == QLatin1Char(op[0]) && m_text[i + 1] == QLatin1Char(op[1]))
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
                return fail(QStringLiteral("Unexpected character '%1'").arg(spelling), i, i + 1);
            token.text = spelling;
            i += qint32(spelling.size());
        }
        token.end = i;
        m_tokens.push_back(std::move(token));
    }
    Token end;
    end.start = end.end = n;
    m_tokens.push_back(end);
    return true;
}

qint32 Parser::addWith(Node node, const qint32 *children, size_t count)
{
    if (qint32(m_formula.nodes.size()) >= MaxFormulaNodes) {
        fail(QStringLiteral("Formula has more than %1 parts").arg(MaxFormulaNodes), node.start, node.end);
        return -1;
    }
    qint32 height = 1;
    for (size_t i = 0; i < count; ++i) height = std::max(height, m_heights[size_t(children[i])] + 1);
    if (height > MaxFormulaDepth) {
        fail(QStringLiteral("Formula is nested more than %1 levels deep").arg(MaxFormulaDepth),
             node.start, node.end);
        return -1;
    }
    m_formula.nodes.push_back(node);
    m_heights.push_back(height);
    return qint32(m_formula.nodes.size()) - 1;
}

qint32 Parser::comparison() { return binaryLevel(0); }

// Levels: 0 comparisons, 1 '&', 2 '+ -', 3 '* /', 4 '^' (all left associative).
qint32 Parser::binaryLevel(int level)
{
    static const std::vector<std::pair<const char *, Op>> levels[] = {
        {{"=", Op::Eq}, {"<>", Op::Ne}, {"<", Op::Lt}, {"<=", Op::Le}, {">", Op::Gt}, {">=", Op::Ge}},
        {{"&", Op::Concat}},
        {{"+", Op::Add}, {"-", Op::Sub}},
        {{"*", Op::Mul}, {"/", Op::Div}},
        {{"^", Op::Pow}},
    };
    auto next = [this, level]() { return level == 4 ? percent() : binaryLevel(level + 1); };
    qint32 left = next();
    while (left >= 0) {
        const auto &candidates = levels[level];
        const auto match = std::find_if(candidates.begin(), candidates.end(),
                                        [this](const auto &entry) { return isOperator(entry.first); });
        if (match == candidates.end()) break;
        take();
        const qint32 right = next();
        if (right < 0) return -1;
        Node node;
        node.kind = Node::Kind::Binary;
        node.op = match->second;
        node.a = left; node.b = right;
        node.start = m_formula.nodes[size_t(left)].start;
        node.end = m_formula.nodes[size_t(right)].end;
        left = add(node, {left, right});
    }
    return left;
}

qint32 Parser::percent()
{
    qint32 node = unary();
    while (node >= 0 && peek().kind == Token::Kind::Percent) {
        const Token token = take();
        Node wrap;
        wrap.kind = Node::Kind::Unary;
        wrap.op = Op::Percent;
        wrap.a = node;
        wrap.start = m_formula.nodes[size_t(node)].start;
        wrap.end = token.end;
        node = add(wrap, {node});
    }
    return node;
}

qint32 Parser::unary()
{
    if (isOperator("-") || isOperator("+")) {
        const Token token = take();
        if (++m_nesting > MaxFormulaNesting) {
            fail(QStringLiteral("Formula nests more than %1 levels").arg(MaxFormulaNesting), token.start, token.end);
            return -1;
        }
        const qint32 child = unary();
        --m_nesting;
        if (child < 0) return -1;
        Node node;
        node.kind = Node::Kind::Unary;
        node.op = token.text == u"-" ? Op::Neg : Op::Plus;
        node.a = child;
        node.start = token.start;
        node.end = m_formula.nodes[size_t(child)].end;
        return add(node, {child});
    }
    return primary();
}

qint32 Parser::sheetIndex(const QString &id)
{
    const qsizetype existing = m_formula.sheets.indexOf(id);
    if (existing >= 0) return qint32(existing);
    m_formula.sheets.append(id);
    return qint32(m_formula.sheets.size() - 1);
}

qint32 Parser::primary()
{
    const Token token = take();
    Node node;
    node.start = token.start;
    node.end = token.end;
    switch (token.kind) {
    case Token::Kind::Number:
        node.kind = Node::Kind::Number;
        node.number = token.number;
        return add(node, {});
    case Token::Kind::String:
        node.kind = Node::Kind::Text;
        node.text = qint32(m_formula.strings.size());
        m_formula.strings.append(token.text);
        return add(node, {});
    case Token::Kind::Error:
        node.kind = Node::Kind::Error;
        node.error = token.error;
        return add(node, {});
    case Token::Kind::LParen: {
        if (++m_nesting > MaxFormulaNesting) {
            fail(QStringLiteral("Formula nests more than %1 levels").arg(MaxFormulaNesting), token.start, token.end);
            return -1;
        }
        const qint32 inner = comparison();
        --m_nesting;
        if (inner < 0) return -1;
        if (peek().kind != Token::Kind::RParen) {
            fail(QStringLiteral("Missing closing parenthesis"), token.start, peek().end);
            return -1;
        }
        node.kind = Node::Kind::Group;
        node.a = inner;
        node.end = take().end;
        return add(node, {inner});
    }
    case Token::Kind::Quoted: {
        if (peek().kind != Token::Kind::Bang) {
            fail(QStringLiteral("A quoted sheet name must be followed by '!'"), token.start, token.end);
            return -1;
        }
        take();
        const QString id = m_lookup ? m_lookup(token.text) : QString();
        if (id.isEmpty()) {
            fail(QStringLiteral("Unknown sheet '%1'").arg(token.text), token.start, token.end);
            return -1;
        }
        return reference(sheetIndex(id), token.start);
    }
    case Token::Kind::Word: {
        if (peek().kind == Token::Kind::LParen) return call(token);
        if (peek().kind == Token::Kind::Bang) {
            take();
            const QString id = m_lookup ? m_lookup(token.text) : QString();
            if (id.isEmpty()) {
                fail(QStringLiteral("Unknown sheet '%1'").arg(token.text), token.start, token.end);
                return -1;
            }
            return reference(sheetIndex(id), token.start);
        }
        if (token.text.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
            || token.text.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0) {
            node.kind = Node::Kind::Boolean;
            node.boolean = token.text.size() == 4;
            return add(node, {});
        }
        --m_pos; // re-read the word as a reference
        return reference(-1, token.start);
    }
    case Token::Kind::End:
        fail(QStringLiteral("Formula ends where a value is expected"), token.start, token.end);
        return -1;
    default:
        fail(QStringLiteral("Expected a value before '%1'").arg(token.text), token.start, token.end);
        return -1;
    }
}

qint32 Parser::reference(qint32 sheet, qint32 start)
{
    const Token first = take();
    RefPoint a;
    if (first.kind != Token::Kind::Word || !parseCellAddress(first.text, &a)) {
        if (first.kind == Token::Kind::Word && sheet < 0)
            fail(QStringLiteral("Unknown name '%1' (names are not supported)").arg(first.text),
                 first.start, first.end);
        else
            fail(QStringLiteral("Expected a cell reference such as A1"), first.start, first.end);
        return -1;
    }
    Node node;
    node.sheet = sheet;
    node.start = start;
    node.end = first.end;
    node.kind = Node::Kind::Cell;
    node.first = a;
    if (peek().kind == Token::Kind::Colon) {
        take();
        const Token second = take();
        RefPoint b;
        if (second.kind != Token::Kind::Word || !parseCellAddress(second.text, &b)) {
            fail(QStringLiteral("Expected a cell reference after ':'"), second.start, second.end);
            return -1;
        }
        node.kind = Node::Kind::Range;
        node.first = a.row <= b.row ? a : b;
        node.last = a.row <= b.row ? b : a;
        const RefPoint left = a.column <= b.column ? a : b, right = a.column <= b.column ? b : a;
        node.first.column = left.column; node.first.columnAbsolute = left.columnAbsolute;
        node.last.column = right.column; node.last.columnAbsolute = right.columnAbsolute;
        node.end = second.end;
    }
    return add(node, {});
}

qint32 Parser::call(const Token &name)
{
    const FunctionSpec *spec = functionByName(name.text);
    if (!spec) {
        fail(QStringLiteral("Unknown function '%1'").arg(name.text.toUpper()), name.start, name.end);
        return -1;
    }
    const Token open = take();
    if (++m_nesting > MaxFormulaNesting) {
        fail(QStringLiteral("Formula nests more than %1 levels").arg(MaxFormulaNesting), open.start, open.end);
        return -1;
    }
    std::vector<qint32> arguments;
    if (peek().kind != Token::Kind::RParen) {
        for (;;) {
            const qint32 argument = comparison();
            if (argument < 0) return -1;
            arguments.push_back(argument);
            if (peek().kind != Token::Kind::Comma) break;
            take();
        }
    }
    --m_nesting;
    if (peek().kind != Token::Kind::RParen) {
        fail(QStringLiteral("Expected ',' or ')' in %1").arg(QLatin1StringView(spec->name)),
             peek().start, peek().end);
        return -1;
    }
    const Token close = take();
    const int count = int(arguments.size());
    if (count < spec->minimumArguments || count > spec->maximumArguments) {
        fail(QStringLiteral("%1 expects %2").arg(QLatin1StringView(spec->name), QLatin1StringView(spec->signature)),
             name.start, close.end);
        return -1;
    }
    Node node;
    node.kind = Node::Kind::Call;
    node.function = spec->id;
    node.a = qint32(m_formula.args.size());
    node.b = count;
    node.start = name.start;
    node.end = close.end;
    m_formula.args.insert(m_formula.args.end(), arguments.begin(), arguments.end());
    return addWith(node, arguments.data(), arguments.size());
}

ParseResult Parser::run()
{
    ParseResult result;
    if (m_text.size() > m_maximumLength) {
        fail(QStringLiteral("Formula is longer than %1 characters").arg(m_maximumLength),
             m_maximumLength, qint32(m_text.size()));
    } else if (!m_text.startsWith(QLatin1Char('='))) {
        fail(QStringLiteral("A formula starts with '='"), 0, 1);
    } else if (lex()) {
        const qint32 root = comparison();
        if (root >= 0 && peek().kind != Token::Kind::End)
            fail(QStringLiteral("Unexpected '%1'").arg(m_text.mid(peek().start, peek().end - peek().start)),
                 peek().start, peek().end);
        m_formula.root = root;
    }
    if (m_failed) { result.error = m_error; return result; }
    result.formula = std::make_shared<const Formula>(std::move(m_formula));
    return result;
}
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

ParseResult parseFormula(const QString &text, const SheetIdLookup &sheetIdForName, int maximumLength)
{
    return Parser(text, sheetIdForName, maximumLength).run();
}

} // namespace QindaTK::Calc
