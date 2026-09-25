// SPDX-License-Identifier: LGPL-3.0-or-later
// Equation text forms (WP-052, D-114): the canonical source, the readable
// linear alternative, the AST dump and the symbol palette.
#include "rich_text_equation.h"

namespace QindaTK::RichText::Equation {

QString displayDelimiter(const QString &source)
{
    if (source == u".") return {};
    if (source == u"\\{") return QStringLiteral("{");
    if (source == u"\\}") return QStringLiteral("}");
    if (source == u"\\langle") return QStringLiteral("⟨");
    if (source == u"\\rangle") return QStringLiteral("⟩");
    if (source == u"\\|") return QStringLiteral("‖");
    return source;
}

QString displayText(const Node &node)
{
    if (node.kind == NodeKind::Operator && node.command.isEmpty() && node.text == u"*") return QStringLiteral("∗");
    return node.text;
}

namespace {

// ---- canonical source

void appendPiece(QString &out, const QString &piece)
{
    if (piece.isEmpty()) return;
    const QChar next = piece.front();
    if (!out.isEmpty()) {
        // A command word would swallow a following letter; adjacent numbers
        // (or a number and a period) would read as one number.
        int letters = 0;
        while (letters < out.size() && isCommandLetter(out.at(out.size() - 1 - letters))) ++letters;
        const bool afterWord = letters > 0 && letters < out.size() && out.at(out.size() - 1 - letters) == u'\\';
        const QChar last = out.back();
        const bool numeric = (last.isDigit() || last == u'.') && (next.isDigit() || next == u'.');
        if ((afterWord && isCommandLetter(next)) || numeric) out += u' ';
    }
    out += piece;
}

QString source(const Node &node, bool braced);

QString rowSource(const Node &row)
{
    QString out;
    for (const Node &child : row.children) appendPiece(out, source(child, true));
    return out;
}

bool singleToken(const Node &node)
{
    switch (node.kind) {
    case NodeKind::Identifier:
    case NodeKind::Operator:
    case NodeKind::Function:
    case NodeKind::LargeOperator:
        return true;
    case NodeKind::Number:
        return node.text.size() == 1;
    default:
        return false;
    }
}

QString scriptArgument(const Node &node)
{
    if (node.kind == NodeKind::Row) return u'{' + rowSource(node) + u'}';
    return singleToken(node) ? source(node, true) : u'{' + source(node, true) + u'}';
}

QString source(const Node &node, bool braced)
{
    switch (node.kind) {
    case NodeKind::Row:
        return braced ? u'{' + rowSource(node) + u'}' : rowSource(node);
    case NodeKind::Identifier:
    case NodeKind::Operator:
    case NodeKind::Function:
    case NodeKind::LargeOperator:
    case NodeKind::Space:
        return node.command.isEmpty() ? node.text : u'\\' + node.command;
    case NodeKind::Number:
        return node.text;
    case NodeKind::Text:
        return QStringLiteral("\\text{%1}").arg(node.text);
    case NodeKind::Fraction:
        return QStringLiteral("\\frac{%1}{%2}").arg(rowSource(node.children.at(0)), rowSource(node.children.at(1)));
    case NodeKind::Root:
        return node.children.size() > 1
            ? QStringLiteral("\\sqrt[%1]{%2}").arg(rowSource(node.children.at(1)), rowSource(node.children.at(0)))
            : QStringLiteral("\\sqrt{%1}").arg(rowSource(node.children.at(0)));
    case NodeKind::Scripts: {
        QString out = source(node.children.at(0), true);
        std::size_t next = 1;
        if (node.hasSub) out += u'_' + scriptArgument(node.children.at(next++));
        if (node.hasSup) out += u'^' + scriptArgument(node.children.at(next));
        return out;
    }
    case NodeKind::Fenced: {
        QString out = QStringLiteral("\\left") + node.text;
        appendPiece(out, rowSource(node.children.at(0)));
        appendPiece(out, QStringLiteral("\\right") + node.close);
        return out;
    }
    }
    return {};
}

// ---- linear text

bool spacedOperator(const Node &node)
{
    static const QString spaced = QStringLiteral("=<>+-±∓×⋅÷∗∘≤≥≠≈≡∼≃∝≪≫→←⇒⇐⇔↦∈∉⊂⊃⊆⊇∪∩∧∨");
    return node.kind == NodeKind::Operator && node.text.size() == 1 && spaced.contains(node.text);
}

QString linear(const Node &node);

// Parentheses around anything but one plain item, so "(a + b)/2" keeps its meaning.
QString wrapped(const Node &node)
{
    const Node &item = node.kind == NodeKind::Row && node.children.size() == 1 ? node.children.front() : node;
    const bool simple = item.kind != NodeKind::Row && item.kind != NodeKind::Scripts && item.kind != NodeKind::Fraction
        && item.kind != NodeKind::Space;
    const QString text = linear(node);
    return simple ? text : u'(' + text + u')';
}

QString linear(const Node &node)
{
    switch (node.kind) {
    case NodeKind::Row: {
        QString out;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const Node &child = node.children.at(i);
            const bool unary = i == 0 || node.children.at(i - 1).kind == NodeKind::Operator;
            if (spacedOperator(child) && !unary) out += u' ' + displayText(child) + u' ';
            else out += linear(child);
            const bool operatorName = child.kind == NodeKind::Function || child.kind == NodeKind::LargeOperator
                || (child.kind == NodeKind::Scripts && (child.children.front().kind == NodeKind::LargeOperator
                                                        || child.children.front().kind == NodeKind::Function));
            const bool separator = child.kind == NodeKind::Operator && (child.text == u"," || child.text == u";");
            if ((operatorName || separator) && i + 1 < node.children.size()) out += u' ';
        }
        return out;
    }
    case NodeKind::Identifier:
    case NodeKind::Number:
    case NodeKind::Operator:
    case NodeKind::Function:
    case NodeKind::LargeOperator:
    case NodeKind::Text:
        return displayText(node);
    case NodeKind::Space:
        return node.command == u"!" ? QString() : QStringLiteral(" ");
    case NodeKind::Fraction:
        return wrapped(node.children.at(0)) + u'/' + wrapped(node.children.at(1));
    case NodeKind::Root:
        return node.children.size() > 1
            ? QStringLiteral("√[%1](%2)").arg(linear(node.children.at(1)), linear(node.children.at(0)))
            : QStringLiteral("√(%1)").arg(linear(node.children.at(0)));
    case NodeKind::Scripts: {
        QString out = wrapped(node.children.at(0));
        std::size_t next = 1;
        if (node.hasSub) out += u'_' + wrapped(node.children.at(next++));
        if (node.hasSup) out += u'^' + wrapped(node.children.at(next));
        return out;
    }
    case NodeKind::Fenced:
        return displayDelimiter(node.text) + linear(node.children.at(0)) + displayDelimiter(node.close);
    }
    return {};
}

const char *kindName(NodeKind kind)
{
    static const char *const names[] = {"row", "id", "num", "op", "fn", "big", "text", "space",
                                        "frac", "root", "scripts", "fenced"};
    return names[int(kind)];
}

} // namespace

QString canonicalSource(const Node &root)
{
    return source(root, false);
}

QString linearText(const Node &root)
{
    return linear(root);
}

QString structure(const Node &node)
{
    QString out = QLatin1String(kindName(node.kind));
    if (node.kind == NodeKind::Scripts) out += QLatin1String(node.hasSub ? "_" : "") + QLatin1String(node.hasSup ? "^" : "");
    if (node.kind == NodeKind::Fenced) out += u':' + node.text + node.close;
    else if (!node.text.isEmpty()) out += u':' + node.text;
    else if (node.kind == NodeKind::Space) out += u':' + node.command;
    if (node.children.empty()) return out;
    QStringList children;
    for (const Node &child : node.children) children.append(structure(child));
    return out + u'[' + children.join(u' ') + u']';
}

const QVector<Symbol> &palette()
{
    static const QVector<Symbol> symbols{
        {QStringLiteral("frac"), QStringLiteral("a⁄b"), QStringLiteral("\\frac{}{}")},
        {QStringLiteral("sqrt"), QStringLiteral("√"), QStringLiteral("\\sqrt{}")},
        {QStringLiteral("sup"), QStringLiteral("x²"), QStringLiteral("^{}")},
        {QStringLiteral("sub"), QStringLiteral("xᵢ"), QStringLiteral("_{}")},
        {QStringLiteral("sum"), QStringLiteral("∑"), QStringLiteral("\\sum_{}^{}")},
        {QStringLiteral("int"), QStringLiteral("∫"), QStringLiteral("\\int_{}^{}")},
        {QStringLiteral("parens"), QStringLiteral("( )"), QStringLiteral("\\left(\\right)")},
        {QStringLiteral("alpha"), QStringLiteral("α"), QStringLiteral("\\alpha")},
        {QStringLiteral("beta"), QStringLiteral("β"), QStringLiteral("\\beta")},
        {QStringLiteral("theta"), QStringLiteral("θ"), QStringLiteral("\\theta")},
        {QStringLiteral("pi"), QStringLiteral("π"), QStringLiteral("\\pi")},
        {QStringLiteral("infty"), QStringLiteral("∞"), QStringLiteral("\\infty")},
        {QStringLiteral("times"), QStringLiteral("×"), QStringLiteral("\\times")},
        {QStringLiteral("pm"), QStringLiteral("±"), QStringLiteral("\\pm")},
        {QStringLiteral("leq"), QStringLiteral("≤"), QStringLiteral("\\leq")},
        {QStringLiteral("geq"), QStringLiteral("≥"), QStringLiteral("\\geq")},
        {QStringLiteral("neq"), QStringLiteral("≠"), QStringLiteral("\\neq")},
    };
    return symbols;
}

QString withSymbol(const QString &source, int cursor, const QString &symbolId)
{
    for (const Symbol &symbol : palette()) {
        if (symbol.id != symbolId) continue;
        const int at = qBound(0, cursor, int(source.size()));
        QString inserted = symbol.source;
        // "\alpha" before a letter would read as one longer command.
        if (at < source.size() && isCommandLetter(inserted.back()) && isCommandLetter(source.at(at))) inserted += u' ';
        return source.left(at) + inserted + source.mid(at);
    }
    return source;
}

} // namespace QindaTK::RichText::Equation
