// SPDX-License-Identifier: LGPL-3.0-or-later
// Equation source (WP-052, D-114): the parser of the documented TeX subset
// with its command tables. Canonical source, linear text and the palette
// are in rich_text_equation_text.cpp; layout and painting in
// rich_text_equation_layout.cpp.
#include "rich_text_equation.h"

#include <QHash>

namespace QindaTK::RichText::Equation {

namespace {

struct Command {
    NodeKind kind;
    QString text;
};

// AGENT-CONTRACT: docs/references-review.md#equations lists these commands;
// a command missing here is refused as Unsupported.
const QHash<QString, Command> &commands()
{
    static const QHash<QString, Command> table = [] {
        QHash<QString, Command> t;
        auto add = [&t](NodeKind kind, const char *names, const QString &texts) {
            const QStringList list = QString::fromLatin1(names).split(u' ');
            for (int i = 0; i < list.size(); ++i) t.insert(list.at(i), {kind, texts.mid(i, 1)});
        };
        add(NodeKind::Identifier, "alpha beta gamma delta epsilon varepsilon zeta eta theta vartheta iota kappa "
                                  "lambda mu nu xi pi varpi rho varrho sigma varsigma tau upsilon phi varphi chi psi omega",
            QStringLiteral("αβγδϵεζηθϑικλμνξπϖρϱσςτυϕφχψω"));
        add(NodeKind::Identifier, "Gamma Delta Theta Lambda Xi Pi Sigma Upsilon Phi Psi Omega infty partial nabla ell hbar emptyset",
            QStringLiteral("ΓΔΘΛΞΠΣΥΦΨΩ∞∂∇ℓℏ∅"));
        add(NodeKind::Operator, "pm mp times cdot div ast circ leq le geq ge neq ne approx equiv sim simeq propto ll gg "
                                "to rightarrow leftarrow Rightarrow Leftarrow Leftrightarrow mapsto in notin subset supset "
                                "subseteq supseteq cup cap forall exists neg wedge vee ldots cdots prime perp parallel angle mid",
            QStringLiteral("±∓×⋅÷∗∘≤≤≥≥≠≠≈≡∼≃∝≪≫→→←⇒⇐⇔↦∈∉⊂⊃⊆⊇∪∩∀∃¬∧∨…⋯′⊥∥∠∣"));
        add(NodeKind::LargeOperator, "sum prod coprod int iint oint bigcup bigcap", QStringLiteral("∑∏∐∫∬∮⋃⋂"));
        for (const char *name : {"sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh",
                                 "cosh", "tanh", "log", "ln", "lg", "exp", "det", "dim", "min", "max", "sup",
                                 "inf", "lim", "gcd", "deg", "arg", "ker", "Pr"})
            t.insert(QLatin1String(name), {NodeKind::Function, QLatin1String(name)});
        for (const char *name : {"quad", "qquad"}) t.insert(QLatin1String(name), {NodeKind::Space, {}});
        return t;
    }();
    return table;
}

const QString &asciiOperators()
{
    static const QString chars = QStringLiteral("+-=<>,;:!'|/*()[].?");
    return chars;
}

// \left and \right delimiters, in source form.
const QStringList &delimiters()
{
    static const QStringList list{QStringLiteral("("), QStringLiteral(")"), QStringLiteral("["), QStringLiteral("]"),
                                  QStringLiteral("|"), QStringLiteral("."), QStringLiteral("\\{"), QStringLiteral("\\}"),
                                  QStringLiteral("\\langle"), QStringLiteral("\\rangle"), QStringLiteral("\\|")};
    return list;
}

class Parser {
public:
    explicit Parser(const QString &source) : m_src(source) {}

    Parsed run()
    {
        if (m_src.size() > MaximumSourceLength)
            fail(ErrorCode::InvalidArgument, QStringLiteral("The equation is longer than %1 characters").arg(MaximumSourceLength), 0);
        Node root = row(0, Stop::End);
        if (failed()) return {std::nullopt, m_error};
        return {std::move(root), {}};
    }

private:
    enum class Stop { End, Brace, Bracket, Right };

    bool failed() const { return m_error.code != ErrorCode::None; }
    void fail(ErrorCode code, const QString &message, int at)
    {
        if (!failed()) m_error = {code, message, at};
    }
    void skipSpace()
    {
        while (m_pos < m_src.size() && m_src.at(m_pos).isSpace()) ++m_pos;
    }
    QString peekCommand() const
    {
        if (m_pos >= m_src.size() || m_src.at(m_pos) != u'\\') return {};
        int end = m_pos + 1;
        while (end < m_src.size() && isCommandLetter(m_src.at(end))) ++end;
        if (end == m_pos + 1 && end < m_src.size()) ++end; // a symbol command such as \, or \{
        return m_src.mid(m_pos + 1, end - m_pos - 1);
    }

    Node row(int depth, Stop stop)
    {
        Node result;
        if (depth > MaximumDepth) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("The equation nests too deeply"), m_pos);
            return result;
        }
        while (!failed()) {
            skipSpace();
            if (m_pos >= m_src.size()) {
                if (stop == Stop::Brace) fail(ErrorCode::InvalidArgument, QStringLiteral("Missing }"), m_pos);
                else if (stop == Stop::Bracket) fail(ErrorCode::InvalidArgument, QStringLiteral("Missing ]"), m_pos);
                else if (stop == Stop::Right) fail(ErrorCode::InvalidArgument, QStringLiteral("\\left without \\right"), m_pos);
                break;
            }
            const QChar c = m_src.at(m_pos);
            if (c == u'}') {
                if (stop != Stop::Brace) fail(ErrorCode::InvalidArgument, QStringLiteral("Unmatched }"), m_pos);
                break;
            }
            if (c == u']' && stop == Stop::Bracket) break;
            if (c == u'\\' && peekCommand() == u"right") {
                if (stop != Stop::Right) fail(ErrorCode::InvalidArgument, QStringLiteral("\\right without \\left"), m_pos);
                break;
            }
            if (c == u'^' || c == u'_') {
                if (result.children.empty()) {
                    fail(ErrorCode::InvalidArgument, QStringLiteral("A script needs something to attach to"), m_pos);
                    break;
                }
                script(result.children.back(), depth);
                continue;
            }
            std::optional<Node> item = atom(depth, false);
            if (!item) break;
            result.children.push_back(std::move(*item));
        }
        return result;
    }

    void script(Node &base, int depth)
    {
        const int at = m_pos;
        const bool sup = m_src.at(m_pos) == u'^';
        ++m_pos;
        if (base.kind != NodeKind::Scripts) {
            Node scripts;
            scripts.kind = NodeKind::Scripts;
            scripts.children.push_back(std::move(base));
            base = std::move(scripts);
        }
        if (sup ? base.hasSup : base.hasSub) {
            fail(ErrorCode::InvalidArgument, sup ? QStringLiteral("Double superscript") : QStringLiteral("Double subscript"), at);
            return;
        }
        Node arg = argument(depth + 1, true);
        if (failed()) return;
        if (sup) {
            base.children.push_back(std::move(arg));
            base.hasSup = true;
        } else {
            base.children.insert(base.children.begin() + 1, std::move(arg));
            base.hasSub = true;
        }
    }

    // A braced group or one token. Script arguments drop the braces of a
    // single item; frac and sqrt arguments are always rows.
    Node argument(int depth, bool unwrap)
    {
        skipSpace();
        const int at = m_pos;
        if (m_pos >= m_src.size() || m_src.at(m_pos) == u'}' || m_src.at(m_pos) == u'^' || m_src.at(m_pos) == u'_') {
            fail(ErrorCode::InvalidArgument, QStringLiteral("Missing argument"), at);
            return {};
        }
        Node result;
        if (m_src.at(m_pos) == u'{') {
            result = group(depth);
        } else {
            std::optional<Node> item = atom(depth, true);
            if (!item) return {};
            result.children.push_back(std::move(*item));
        }
        if (unwrap && result.children.size() == 1) return std::move(result.children.front());
        return result;
    }

    Node group(int depth)
    {
        ++m_pos; // '{'
        Node result = row(depth + 1, Stop::Brace);
        if (!failed()) ++m_pos; // '}'
        return result;
    }

    std::optional<Node> atom(int depth, bool single)
    {
        const int at = m_pos;
        const QChar c = m_src.at(m_pos);
        Node node;
        if (c == u'{') {
            Node inner = group(depth);
            if (failed()) return std::nullopt;
            // A group of one item is that item; braces carry no meaning.
            if (inner.children.size() == 1) return std::move(inner.children.front());
            return inner;
        }
        if (c.isDigit() && c.unicode() < 128) {
            node.kind = NodeKind::Number;
            int end = m_pos + 1;
            if (!single) {
                while (end < m_src.size() && m_src.at(end).isDigit() && m_src.at(end).unicode() < 128) ++end;
                if (end + 1 < m_src.size() && m_src.at(end) == u'.' && m_src.at(end + 1).isDigit()) {
                    end += 2;
                    while (end < m_src.size() && m_src.at(end).isDigit() && m_src.at(end).unicode() < 128) ++end;
                }
            }
            node.text = m_src.mid(m_pos, end - m_pos);
            m_pos = end;
            return node;
        }
        if (c.isHighSurrogate() || c.isLowSurrogate()) {
            fail(ErrorCode::Unsupported, QStringLiteral("Characters outside the Basic Multilingual Plane are outside the supported equation subset"), at);
            return std::nullopt;
        }
        if (c.isLetter()) {
            node.kind = NodeKind::Identifier;
            node.text = c;
            ++m_pos;
            return node;
        }
        if (c == u'\'') {
            node.kind = NodeKind::Operator;
            node.text = QStringLiteral("′");
            node.command = QStringLiteral("prime");
            ++m_pos;
            return node;
        }
        if (asciiOperators().contains(c) || (c.unicode() >= 128 && c.category() == QChar::Symbol_Math)) {
            node.kind = NodeKind::Operator;
            node.text = c;
            ++m_pos;
            return node;
        }
        if (c == u'\\') return command(depth);
        fail(ErrorCode::Unsupported, QStringLiteral("'%1' is outside the supported equation subset").arg(c), at);
        return std::nullopt;
    }

    std::optional<Node> command(int depth)
    {
        const int at = m_pos;
        const QString name = peekCommand();
        if (name.isEmpty()) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("A command name is missing after \\"), at);
            return std::nullopt;
        }
        m_pos += int(name.size()) + 1;
        Node node;
        node.command = name;
        static const QHash<QString, QString> symbols{{QStringLiteral("{"), QStringLiteral("{")},
                                                     {QStringLiteral("}"), QStringLiteral("}")},
                                                     {QStringLiteral("|"), QStringLiteral("‖")}};
        if (symbols.contains(name)) {
            node.kind = NodeKind::Operator;
            node.text = symbols.value(name);
            return node;
        }
        if (name == u"," || name == u":" || name == u";" || name == u"!") {
            node.kind = NodeKind::Space;
            return node;
        }
        if (name == u"frac") {
            node.kind = NodeKind::Fraction;
            node.children.push_back(argument(depth + 1, false));
            if (!failed()) node.children.push_back(argument(depth + 1, false));
            return failed() ? std::nullopt : std::optional<Node>(std::move(node));
        }
        if (name == u"sqrt") {
            node.kind = NodeKind::Root;
            skipSpace();
            std::optional<Node> index;
            if (m_pos < m_src.size() && m_src.at(m_pos) == u'[') {
                ++m_pos;
                index = row(depth + 1, Stop::Bracket);
                if (failed()) return std::nullopt;
                ++m_pos; // ']'
            }
            node.children.push_back(argument(depth + 1, false));
            if (index) node.children.push_back(std::move(*index));
            return failed() ? std::nullopt : std::optional<Node>(std::move(node));
        }
        if (name == u"left") return fenced(depth, at);
        if (name == u"text" || name == u"mathrm") return text(name);
        const auto known = commands().constFind(name);
        if (known == commands().constEnd()) {
            fail(ErrorCode::Unsupported, QStringLiteral("\\%1 is outside the supported equation subset").arg(name), at);
            return std::nullopt;
        }
        node.kind = known->kind;
        node.text = known->text;
        return node;
    }

    QString delimiter()
    {
        skipSpace();
        const int at = m_pos;
        for (const QString &candidate : delimiters())
            if (QStringView(m_src).mid(m_pos).startsWith(candidate)
                && !(candidate.size() > 2 && m_pos + candidate.size() < m_src.size() && isCommandLetter(m_src.at(m_pos + candidate.size())))) {
                m_pos += int(candidate.size());
                return candidate;
            }
        fail(ErrorCode::Unsupported, QStringLiteral("This \\left or \\right delimiter is outside the supported equation subset"), at);
        return {};
    }

    std::optional<Node> fenced(int depth, int at)
    {
        Node node;
        node.kind = NodeKind::Fenced;
        node.command = QStringLiteral("left");
        node.text = delimiter();
        if (failed()) return std::nullopt;
        node.children.push_back(row(depth + 1, Stop::Right));
        if (failed()) return std::nullopt;
        m_pos += 6; // "\right"
        node.close = delimiter();
        if (failed()) return std::nullopt;
        Q_UNUSED(at);
        return node;
    }

    std::optional<Node> text(const QString &name)
    {
        skipSpace();
        const int at = m_pos;
        if (m_pos >= m_src.size() || m_src.at(m_pos) != u'{') {
            fail(ErrorCode::InvalidArgument, QStringLiteral("\\%1 needs {text}").arg(name), at);
            return std::nullopt;
        }
        const int end = int(m_src.indexOf(u'}', m_pos + 1));
        if (end < 0) {
            fail(ErrorCode::InvalidArgument, QStringLiteral("Missing }"), int(m_src.size()));
            return std::nullopt;
        }
        const QString content = m_src.mid(m_pos + 1, end - m_pos - 1);
        if (content.contains(u'{') || content.contains(u'\\')) {
            fail(ErrorCode::Unsupported, QStringLiteral("Commands and groups inside \\%1 are outside the supported equation subset").arg(name), at);
            return std::nullopt;
        }
        m_pos = end + 1;
        Node node;
        node.kind = NodeKind::Text;
        node.command = QStringLiteral("text");
        node.text = content;
        return node;
    }

    const QString m_src;
    int m_pos = 0;
    ParseError m_error;
};

} // namespace

Parsed parse(const QString &source)
{
    return Parser(source).run();
}

} // namespace QindaTK::RichText::Equation
