// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_equation.h"

// Visual editing of an equation's expression tree (WP-052, OFF-09). The
// editor keeps a normalized tree: every slot (fraction part, radicand,
// root index, script, fence content) is a Row, and rows do not nest
// directly. Its caret is a position in one row; a selection is a range of
// one row. Every edit re-derives the canonical source, and a source edit
// re-derives the tree, so visual and source edits round-trip:
// normalized(parse(source())) == root().
// AGENT-CONTRACT: docs/references-review.md#visual-editing lists the keys
// and gestures; tst_rich_text_equation_editor checks them and the round trip.
namespace QindaTK::RichText::Equation {

Node normalized(Node node);   // slots as rows, nested rows flattened
Node denormalized(Node node); // the parser's shape of a normalized tree

struct Caret {
    QVector<int> row;         // child indices from the root to a Row
    int index = 0;            // 0..children
    friend bool operator==(const Caret &, const Caret &) = default;
};

class Editor {
public:
    enum class Move { Left, Right, Up, Down, Home, End };

    Editor();
    // Replaces the tree; false (tree unchanged) when `source` does not parse.
    bool setSource(const QString &source, ParseError *error = nullptr);
    QString source() const;
    const Node &root() const { return m_root; }
    Caret caret() const { return m_caret; }
    Caret anchor() const { return m_anchor; }
    bool hasSelection() const { return m_anchor.index != m_caret.index; }
    QString selectedSource() const;
    QString caretPath() const;              // "1.0:2": row path, then the index
    void setCaret(const Caret &caret, bool extend = false);
    bool move(Move move, bool extend = false);
    void selectAll();
    // Typing: letters, digits, operators; ^ _ start a script and / a fraction.
    bool insertText(const QString &text);
    // A TeX-subset fragment at the caret; a selection moves into its first
    // empty slot, and the caret goes to the next empty slot (palette entries).
    bool insertSource(const QString &fragment);
    bool insertSymbol(const QString &symbolId);
    bool backspace();
    bool deleteForward();
    // The caret stop of an editing layout nearest to `point` (box coordinates).
    static Caret caretAt(const Box &box, const QPointF &point);

private:
    Node &row(const QVector<int> &path);
    const Node &row(const QVector<int> &path) const;
    bool commit(const Node &before, const Caret &caret);
    void enter(const QVector<int> &nodePath, bool atEnd, bool forward);
    bool leave(bool forward);
    bool typeChar(QChar c);
    bool script(bool superscript);
    bool fraction();
    bool removeSelection();

    Node m_root;
    Caret m_caret;
    Caret m_anchor;
};

} // namespace QindaTK::RichText::Equation
