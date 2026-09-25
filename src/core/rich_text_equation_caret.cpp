// SPDX-License-Identifier: LGPL-3.0-or-later
// Caret and selection movement of the visual equation editor (WP-052,
// OFF-09): the caret walks the slots of the normalized tree; a selection
// stays inside one row. The keys are listed in
// docs/references-review.md#visual-editing.
#include "rich_text_equation_editor_p.h"

#include <cmath>
#include <limits>

namespace QindaTK::RichText::Equation {

QString Editor::caretPath() const
{
    QStringList parts;
    for (int index : m_caret.row) parts.append(QString::number(index));
    return parts.join(u'.') + u':' + QString::number(m_caret.index);
}

void Editor::setCaret(const Caret &requested, bool extend)
{
    // A caret whose row no longer exists falls back to the end of the equation.
    Caret caret = requested;
    const Node *node = &m_root;
    for (int index : caret.row) {
        if (index < 0 || index >= int(node->children.size())) {
            node = nullptr;
            break;
        }
        node = &node->children.at(std::size_t(index));
    }
    if (!node || !isRow(*node)) {
        caret = {{}, int(m_root.children.size())};
        node = &m_root;
    }
    caret.index = qBound(0, caret.index, int(node->children.size()));
    m_caret = caret;
    if (!extend || !(m_anchor.row == caret.row)) m_anchor = caret;
    else m_anchor.index = qBound(0, m_anchor.index, int(node->children.size()));
}

QString Editor::selectedSource() const
{
    if (!hasSelection()) return {};
    const Node &r = row(m_caret.row);
    const int from = qMin(m_anchor.index, m_caret.index);
    const int to = qMax(m_anchor.index, m_caret.index);
    return canonicalSource(denormalized(makeRow({r.children.begin() + from, r.children.begin() + to})));
}

void Editor::selectAll()
{
    m_anchor = {{}, 0};
    m_caret = {{}, int(m_root.children.size())};
}

void Editor::enter(const QVector<int> &nodePath, bool atEnd, bool forward)
{
    const Node &node = row(nodePath); // any node: row() follows the path
    const QVector<int> slotList = slotsOf(node);
    QVector<int> path = nodePath;
    path.append(forward ? slotList.first() : slotList.last());
    setCaret({path, atEnd ? int(row(path).children.size()) : 0});
}

bool Editor::leave(bool forward)
{
    const QVector<int> path = m_caret.row;
    if (path.isEmpty()) return false;
    const QVector<int> owner = path.mid(0, path.size() - 1);
    const QVector<int> slotList = slotsOf(row(owner));
    const qsizetype at = slotList.indexOf(path.last());
    const qsizetype next = at + (forward ? 1 : -1);
    if (next >= 0 && next < slotList.size()) {
        QVector<int> target = owner;
        target.append(slotList.at(next));
        setCaret({target, forward ? 0 : int(row(target).children.size())});
        return true;
    }
    const QVector<int> parent = owner.mid(0, owner.size() - 1);
    setCaret({parent, owner.last() + (forward ? 1 : 0)});
    return true;
}

bool Editor::move(Move move, bool extend)
{
    const Node &r = row(m_caret.row);
    const int n = int(r.children.size());
    if (extend) {
        // A selection stays inside one row.
        Caret next = m_caret;
        if (move == Move::Left) next.index = qMax(0, next.index - 1);
        else if (move == Move::Right) next.index = qMin(n, next.index + 1);
        else if (move == Move::Home) next.index = 0;
        else if (move == Move::End) next.index = n;
        else return false;
        setCaret(next, true);
        return true;
    }
    if (hasSelection() && (move == Move::Left || move == Move::Right)) {
        const int edge = move == Move::Left ? qMin(m_anchor.index, m_caret.index) : qMax(m_anchor.index, m_caret.index);
        setCaret({m_caret.row, edge});
        return true;
    }
    QVector<int> here = m_caret.row;
    switch (move) {
    case Move::Home: setCaret({here, 0}); return true;
    case Move::End: setCaret({here, n}); return true;
    case Move::Right:
        if (m_caret.index < n) {
            here.append(m_caret.index);
            if (!slotsOf(r.children.at(std::size_t(m_caret.index))).isEmpty()) enter(here, false, true);
            else setCaret({m_caret.row, m_caret.index + 1});
            return true;
        }
        return leave(true);
    case Move::Left:
        if (m_caret.index > 0) {
            here.append(m_caret.index - 1);
            if (!slotsOf(r.children.at(std::size_t(m_caret.index - 1))).isEmpty()) enter(here, true, false);
            else setCaret({m_caret.row, m_caret.index - 1});
            return true;
        }
        return leave(false);
    case Move::Up:
    case Move::Down: {
        // The nearest enclosing structure with a slot above (or below).
        const bool up = move == Move::Up;
        QVector<int> path = m_caret.row;
        while (!path.isEmpty()) {
            const QVector<int> owner = path.mid(0, path.size() - 1);
            const Node &node = row(owner);
            const int slot = path.last();
            int target = -1;
            if (node.kind == NodeKind::Fraction && slot == (up ? 1 : 0)) target = up ? 0 : 1;
            if (node.kind == NodeKind::Root && node.children.size() > 1 && slot == (up ? 0 : 1)) target = up ? 1 : 0;
            if (node.kind == NodeKind::Scripts) {
                const int sub = node.hasSub ? 1 : -1;
                const int sup = node.hasSup ? int(node.children.size()) - 1 : -1;
                target = up ? (slot != sup ? sup : -1) : (slot != sub ? sub : -1);
            }
            if (target >= 0) {
                QVector<int> next = owner;
                next.append(target);
                setCaret({next, m_caret.index});
                return true;
            }
            path = owner.mid(0, owner.size() - 1);
        }
        return false;
    }
    }
    return false;
}

Caret Editor::caretAt(const Box &box, const QPointF &point)
{
    Caret best;
    double bestScore = std::numeric_limits<double>::max();
    for (const Stop &stop : box.stops) {
        const double dy = point.y() < stop.top ? stop.top - point.y() : point.y() > stop.bottom ? point.y() - stop.bottom : 0;
        // Inside a row's extent the horizontal distance decides; a deeper
        // (shorter) row wins a tie, so a click in a script lands in it.
        const double score = std::abs(point.x() - stop.x) + 3 * dy + 0.01 * (stop.bottom - stop.top);
        if (score < bestScore) {
            bestScore = score;
            best = {stop.row, stop.index};
        }
    }
    return best;
}

} // namespace QindaTK::RichText::Equation
