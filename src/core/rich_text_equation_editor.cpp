// SPDX-License-Identifier: LGPL-3.0-or-later
// Visual equation editing on the expression tree (WP-052, OFF-09): typing,
// scripts, fractions, palette templates and deletion; caret movement is in
// rich_text_equation_caret.cpp. Every edit is checked by a round trip
// through the canonical source before it is kept (Editor::commit), so the
// visual view can never hold a tree the source view could not write.
#include "rich_text_equation_editor_p.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace QindaTK::RichText::Equation {

namespace {

bool allSlotsEmpty(const Node &node)
{
    for (int slot : slotsOf(node))
        if (!node.children.at(slot).children.empty()) return false;
    return node.kind != NodeKind::Scripts || isRow(node.children.front());
}

// The nodes a structure unwraps to when its first slot is deleted from.
std::vector<Node> contents(const Node &node, int upToSlot, int *before)
{
    std::vector<Node> out;
    *before = -1;
    auto add = [&](const Node &part, int child) {
        if (child == upToSlot) *before = int(out.size());
        if (isRow(part)) out.insert(out.end(), part.children.begin(), part.children.end());
        else out.push_back(part);
    };
    if (node.kind == NodeKind::Scripts) {
        for (int i = 0; i < int(node.children.size()); ++i) add(node.children.at(i), i);
    } else {
        for (int slot : slotsOf(node)) add(node.children.at(slot), slot);
    }
    if (*before < 0) *before = 0;
    return out;
}

} // namespace

Node normalized(Node node)
{
    for (Node &child : node.children) child = normalized(std::move(child));
    if (node.kind == NodeKind::Row) {
        std::vector<Node> flat;
        for (Node &child : node.children) {
            if (isRow(child)) flat.insert(flat.end(), child.children.begin(), child.children.end());
            else flat.push_back(std::move(child));
        }
        node.children = std::move(flat);
    } else if (node.kind == NodeKind::Scripts) {
        for (std::size_t i = 1; i < node.children.size(); ++i)
            if (!isRow(node.children.at(i))) node.children[i] = makeRow({node.children.at(i)});
    }
    return node;
}

Node denormalized(Node node)
{
    for (Node &child : node.children) child = denormalized(std::move(child));
    // The parser drops the braces of a single-item script argument or base.
    if (node.kind == NodeKind::Scripts)
        for (Node &child : node.children)
            if (isRow(child) && child.children.size() == 1) child = Node(child.children.front());
    return node;
}

Editor::Editor() = default;

bool Editor::setSource(const QString &source, ParseError *error)
{
    const Parsed parsed = parse(source);
    if (!parsed.root) {
        if (error) *error = parsed.error;
        return false;
    }
    m_root = normalized(*parsed.root);
    setCaret(m_caret);
    return true;
}

QString Editor::source() const { return canonicalSource(denormalized(m_root)); }

Node &Editor::row(const QVector<int> &path)
{
    Node *node = &m_root;
    for (int index : path) node = &node->children[std::size_t(index)];
    return *node;
}

const Node &Editor::row(const QVector<int> &path) const
{
    const Node *node = &m_root;
    for (int index : path) node = &node->children.at(std::size_t(index));
    return *node;
}

bool Editor::commit(const Node &before, const Caret &caret)
{
    // AGENT-GUARD: the tree must survive its own source; otherwise the
    // edit is undone here and the caller reports it refused.
    const Parsed parsed = parse(source());
    if (parsed.root && normalized(*parsed.root) == m_root) return true;
    m_root = before;
    m_caret = m_anchor = caret;
    return false;
}

bool Editor::removeSelection()
{
    if (!hasSelection()) return false;
    Node &r = row(m_caret.row);
    const int from = qMin(m_anchor.index, m_caret.index);
    const int to = qMax(m_anchor.index, m_caret.index);
    r.children.erase(r.children.begin() + from, r.children.begin() + to);
    setCaret({m_caret.row, from});
    return true;
}

bool Editor::script(bool superscript)
{
    removeSelection();
    Node &r = row(m_caret.row);
    const int index = m_caret.index;
    if (index == 0) return false; // a script needs something to attach to
    Node &base = r.children[std::size_t(index - 1)];
    if (base.kind != NodeKind::Scripts) {
        Node scripts;
        scripts.kind = NodeKind::Scripts;
        scripts.children.push_back(base);
        base = scripts;
    }
    int slot = -1;
    if (superscript) {
        if (!base.hasSup) base.children.push_back(makeRow()), base.hasSup = true;
        slot = int(base.children.size()) - 1;
    } else {
        if (!base.hasSub) base.children.insert(base.children.begin() + 1, makeRow()), base.hasSub = true;
        slot = 1;
    }
    QVector<int> path = m_caret.row;
    path.append(index - 1);
    path.append(slot);
    setCaret({path, int(row(path).children.size())});
    return true;
}

bool Editor::fraction()
{
    Node &r = row(m_caret.row);
    std::vector<Node> numerator;
    int at = m_caret.index;
    if (hasSelection()) {
        const int from = qMin(m_anchor.index, m_caret.index);
        const int to = qMax(m_anchor.index, m_caret.index);
        numerator.assign(r.children.begin() + from, r.children.begin() + to);
        r.children.erase(r.children.begin() + from, r.children.begin() + to);
        at = from;
    } else if (at > 0 && r.children.at(std::size_t(at - 1)).kind != NodeKind::Operator) {
        // The term before the caret becomes the numerator.
        numerator.push_back(r.children.at(std::size_t(at - 1)));
        r.children.erase(r.children.begin() + at - 1);
        --at;
    }
    Node fraction;
    fraction.kind = NodeKind::Fraction;
    fraction.command = QStringLiteral("frac"); // as the parser names it
    const bool filled = !numerator.empty();
    fraction.children = {makeRow(std::move(numerator)), makeRow()};
    r.children.insert(r.children.begin() + at, fraction);
    QVector<int> path = m_caret.row;
    path.append(at);
    path.append(filled ? 1 : 0);
    setCaret({path, 0});
    return true;
}

bool Editor::typeChar(QChar c)
{
    if (c.isSpace()) return true;
    if (c == u'^' || c == u'_') return script(c == u'^');
    if (c == u'/') return fraction();
    removeSelection();
    Node &r = row(m_caret.row);
    const std::size_t index = std::size_t(m_caret.index);
    Node *previous = index > 0 ? &r.children[index - 1] : nullptr;
    Node node;
    if (c.isDigit() && c.unicode() < 128) {
        if (previous && previous->kind == NodeKind::Number) {
            previous->text += c;
            return true;
        }
        // "3" "." "1" reads as the number 3.1.
        if (previous && index > 1 && previous->kind == NodeKind::Operator && previous->text == u"."
            && previous->command.isEmpty() && r.children.at(index - 2).kind == NodeKind::Number
            && !r.children.at(index - 2).text.contains(u'.')) {
            r.children[index - 2].text += u'.' + QString(c);
            r.children.erase(r.children.begin() + index - 1);
            setCaret({m_caret.row, int(index) - 1});
            return true;
        }
        node.kind = NodeKind::Number;
        node.text = c;
    } else if (c.isLetter() && !c.isSurrogate()) {
        node.kind = NodeKind::Identifier;
        node.text = c;
    } else if (c == u'\'') {
        node.kind = NodeKind::Operator;
        node.text = QStringLiteral("′");
        node.command = QStringLiteral("prime");
    } else if (QStringLiteral("+-=<>,;:!|*()[].?").contains(c) || (c.unicode() >= 128 && c.category() == QChar::Symbol_Math)) {
        node.kind = NodeKind::Operator;
        node.text = c;
    } else {
        return false;
    }
    r.children.insert(r.children.begin() + qsizetype(index), node);
    setCaret({m_caret.row, int(index) + 1});
    return true;
}

bool Editor::insertText(const QString &text)
{
    const Node before = m_root;
    const Caret caret = m_caret;
    for (const QChar c : text)
        if (!typeChar(c)) {
            m_root = before;
            m_caret = m_anchor = caret;
            return false;
        }
    return commit(before, caret);
}

bool Editor::insertSource(const QString &fragment)
{
    const Parsed parsed = parse(fragment);
    if (!parsed.root) return false;
    const Node before = m_root;
    const Caret caret = m_caret;
    std::vector<Node> selected;
    if (hasSelection()) {
        const Node &r = row(m_caret.row);
        const int from = qMin(m_anchor.index, m_caret.index);
        const int to = qMax(m_anchor.index, m_caret.index);
        selected.assign(r.children.begin() + from, r.children.begin() + to);
        removeSelection();
    }
    const Node inserted = normalized(*parsed.root);
    const int at = m_caret.index;
    const int count = int(inserted.children.size());
    Node &r = row(m_caret.row);
    r.children.insert(r.children.begin() + at, inserted.children.begin(), inserted.children.end());
    // The first empty slot of the inserted nodes, depth first, in visual order.
    std::function<bool(const QVector<int> &, QVector<int> *)> emptySlot = [&](const QVector<int> &path, QVector<int> *out) {
        for (int slot : slotsOf(row(path))) {
            QVector<int> child = path;
            child.append(slot);
            if (row(child).children.empty()) return *out = child, true;
            for (int i = 0; i < int(row(child).children.size()); ++i) {
                QVector<int> nested = child;
                nested.append(i);
                if (emptySlot(nested, out)) return true;
            }
        }
        return false;
    };
    auto firstEmpty = [&](QVector<int> *out) {
        for (int i = at; i < at + count; ++i) {
            QVector<int> path = m_caret.row;
            path.append(i);
            if (emptySlot(path, out)) return true;
        }
        return false;
    };
    QVector<int> slot;
    if (!selected.empty() && firstEmpty(&slot)) row(slot).children = selected;
    if (firstEmpty(&slot)) setCaret({slot, 0});
    else setCaret({m_caret.row, at + count});
    return commit(before, caret);
}

bool Editor::insertSymbol(const QString &symbolId)
{
    if (symbolId == u"sup" || symbolId == u"sub") {
        const Node before = m_root;
        const Caret caret = m_caret;
        return script(symbolId == u"sup") && commit(before, caret);
    }
    for (const Symbol &symbol : palette())
        if (symbol.id == symbolId) return insertSource(symbol.source);
    return false;
}

bool Editor::backspace()
{
    const Node before = m_root;
    const Caret caret = m_caret;
    if (removeSelection()) return commit(before, caret);
    Node &r = row(m_caret.row);
    const int index = m_caret.index;
    if (index > 0) {
        Node &previous = r.children[std::size_t(index - 1)];
        if (!slotsOf(previous).isEmpty() && !allSlotsEmpty(previous)) {
            QVector<int> path = m_caret.row;
            path.append(index - 1);
            enter(path, true, false); // step into the structure first
            return true;
        }
        if (previous.kind == NodeKind::Number && previous.text.size() > 1) {
            previous.text.chop(1);
            if (previous.text.endsWith(u'.')) {
                previous.text.chop(1);
                Node dot;
                dot.kind = NodeKind::Operator;
                dot.text = QStringLiteral(".");
                r.children.insert(r.children.begin() + index, dot);
                setCaret({m_caret.row, index + 1});
            }
            return commit(before, caret);
        }
        r.children.erase(r.children.begin() + index - 1);
        setCaret({m_caret.row, index - 1});
        return commit(before, caret);
    }
    if (m_caret.row.isEmpty()) return false;
    const QVector<int> owner = m_caret.row.mid(0, m_caret.row.size() - 1);
    const QVector<int> slotList = slotsOf(row(owner));
    if (slotList.indexOf(m_caret.row.last()) > 0) return leave(false);
    // At the start of the first slot: the structure unwraps into its row.
    const Node structure = row(owner);
    int position = 0;
    const std::vector<Node> parts = contents(structure, m_caret.row.last(), &position);
    const QVector<int> parent = owner.mid(0, owner.size() - 1);
    const int k = owner.last();
    Node &into = row(parent);
    into.children.erase(into.children.begin() + k);
    into.children.insert(into.children.begin() + k, parts.begin(), parts.end());
    setCaret({parent, k + position});
    return commit(before, caret);
}

bool Editor::deleteForward()
{
    const Node before = m_root;
    const Caret caret = m_caret;
    if (removeSelection()) return commit(before, caret);
    Node &r = row(m_caret.row);
    const int index = m_caret.index;
    if (index >= int(r.children.size())) return false;
    Node &next = r.children[std::size_t(index)];
    if (!slotsOf(next).isEmpty() && !allSlotsEmpty(next)) {
        QVector<int> path = m_caret.row;
        path.append(index);
        enter(path, false, true);
        return true;
    }
    if (next.kind == NodeKind::Number && next.text.size() > 1 && !next.text.mid(1).startsWith(u'.')) {
        next.text.remove(0, 1);
        return commit(before, caret);
    }
    r.children.erase(r.children.begin() + index);
    return commit(before, caret);
}

} // namespace QindaTK::RichText::Equation
