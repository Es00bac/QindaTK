// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_equation_editor.h"

// Tree helpers shared by the equation editor's edit and caret files.
namespace QindaTK::RichText::Equation {

inline Node makeRow(std::vector<Node> children = {})
{
    Node row;
    row.children = std::move(children);
    return row;
}

inline bool isRow(const Node &node) { return node.kind == NodeKind::Row; }

// Child indices of a node's slots (rows the caret can enter), in visual order.
inline QVector<int> slotsOf(const Node &node)
{
    switch (node.kind) {
    case NodeKind::Fraction: return {0, 1};
    case NodeKind::Root: return node.children.size() > 1 ? QVector<int>{1, 0} : QVector<int>{0};
    case NodeKind::Fenced: return {0};
    case NodeKind::Scripts: {
        QVector<int> slotList;
        if (isRow(node.children.front())) slotList.append(0);
        for (int i = 1; i < int(node.children.size()); ++i) slotList.append(i);
        return slotList;
    }
    default: return {};
    }
}

} // namespace QindaTK::RichText::Equation
