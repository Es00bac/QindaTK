// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_lexer_p.h"

#include <algorithm>

namespace QindaTK::Calc {
namespace {

bool isLetter(QChar c) { return isFormulaLetter(c); }
bool isDigitChar(QChar c) { return isFormulaDigit(c); }

// "$C" / "AB" — one axis of a whole-column range.
bool parseColumnWord(QStringView text, qint32 *column, bool *absolute)
{
    *absolute = text.startsWith(u'$');
    const QStringView letters = *absolute ? text.mid(1) : text;
    if (letters.isEmpty() || letters.size() > 3 || !std::all_of(letters.begin(), letters.end(), isLetter))
        return false;
    *column = columnFromName(letters);
    return *column >= 0;
}
// "$12" / "12" — one axis of a whole-row range.
bool parseRowWord(QStringView text, qint32 *row, bool *absolute)
{
    *absolute = text.startsWith(u'$');
    const QStringView digits = *absolute ? text.mid(1) : text;
    if (digits.isEmpty() || digits.size() > 7 || !std::all_of(digits.begin(), digits.end(), isDigitChar)
        || digits.front() == u'0') return false;
    const qint64 value = digits.toLongLong();
    if (value < 1 || value > MaxRows) return false;
    *row = qint32(value - 1);
    return true;
}

class Parser {
public:
    Parser(const QString &text, const SheetIdLookup &lookup, int maximumLength, const NameLookup &names)
        : m_text(text), m_lookup(lookup), m_names(names), m_maximumLength(maximumLength) {}
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
    qint32 wholeReference(qint32 sheet, qint32 start, const Token &first, bool columns);
    qint32 name(const Token &token);
    qint32 sheetIndex(const QString &id);
    QString source(const Token &token) const
    { return token.kind == Token::Kind::Word ? token.text : m_text.mid(token.start, token.end - token.start); }

    const QString &m_text;
    const SheetIdLookup &m_lookup;
    const NameLookup &m_names;
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
    ParseError error;
    if (lexFormula(m_text, &m_tokens, &error)) return true;
    return fail(error.message, error.start, error.end);
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
        if (peek().kind == Token::Kind::Colon) { // 2:5
            --m_pos;
            return reference(-1, token.start);
        }
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
        qint32 axis = 0;
        bool absolute = false;
        if (looksLikeCellAddress(token.text)
            || (peek().kind == Token::Kind::Colon && (parseColumnWord(token.text, &axis, &absolute)
                                                      || parseRowWord(token.text, &axis, &absolute)))) {
            --m_pos; // re-read the word as a reference
            return reference(-1, token.start);
        }
        return name(token);
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
    const bool word = first.kind == Token::Kind::Word, number = first.kind == Token::Kind::Number;
    qint32 axis = 0;
    bool absolute = false;
    if ((word || number) && peek().kind == Token::Kind::Colon) {
        if (word && parseColumnWord(first.text, &axis, &absolute)) return wholeReference(sheet, start, first, true);
        if (parseRowWord(source(first), &axis, &absolute)) return wholeReference(sheet, start, first, false);
    }
    RefPoint a;
    if (!word || !parseCellAddress(first.text, &a)) {
        if (word && looksLikeCellAddress(first.text))
            fail(QStringLiteral("'%1' is outside the sheet grid A1:XFD1048576").arg(first.text), first.start, first.end);
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

// A:C or 2:5 — `first` was taken and a ':' follows.
qint32 Parser::wholeReference(qint32 sheet, qint32 start, const Token &first, bool columns)
{
    take(); // ':'
    const Token second = take();
    qint32 a = 0, b = 0;
    bool absoluteA = false, absoluteB = false;
    if (columns) parseColumnWord(first.text, &a, &absoluteA);
    else parseRowWord(source(first), &a, &absoluteA);
    const bool secondOk = columns
        ? second.kind == Token::Kind::Word && parseColumnWord(second.text, &b, &absoluteB)
        : (second.kind == Token::Kind::Word || second.kind == Token::Kind::Number)
              && parseRowWord(source(second), &b, &absoluteB);
    if (!secondOk) {
        fail(columns ? QStringLiteral("Expected a column such as B after ':'")
                     : QStringLiteral("Expected a row number after ':'"), second.start, second.end);
        return -1;
    }
    Node node;
    node.kind = Node::Kind::Range;
    node.whole = columns ? Node::Whole::Columns : Node::Whole::Rows;
    node.sheet = sheet;
    node.start = start;
    node.end = second.end;
    if (b < a) { std::swap(a, b); std::swap(absoluteA, absoluteB); }
    if (columns) {
        node.first = {0, a, false, absoluteA};
        node.last = {MaxRows - 1, b, false, absoluteB};
    } else {
        node.first = {a, 0, absoluteA, false};
        node.last = {b, MaxColumns - 1, absoluteB, false};
    }
    return add(node, {});
}

qint32 Parser::name(const Token &token)
{
    if (!nameProblem(token.text).isEmpty()) {
        fail(QStringLiteral("'%1' is not a valid name or reference").arg(token.text), token.start, token.end);
        return -1;
    }
    const QString defined = m_names ? m_names(token.text) : QString();
    Node node;
    node.kind = Node::Kind::Name;
    node.start = token.start;
    node.end = token.end;
    node.text = qint32(m_formula.strings.size());
    m_formula.strings.append(defined.isEmpty() ? token.text : defined);
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

ParseResult parseFormula(const QString &text, const SheetIdLookup &sheetIdForName, int maximumLength,
                         const NameLookup &names)
{
    return Parser(text, sheetIdForName, maximumLength, names).run();
}

} // namespace QindaTK::Calc
