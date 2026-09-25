// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "rich_text_types.h"
#include <QPolygonF>
#include <QRectF>
#include <QStringList>
#include <optional>
#include <vector>

class QPainter;

// Equations of the rich-text model (WP-052, D-114): a documented TeX subset
// parsed into an AST, a canonical source and a readable linear text, and a
// box layout painted into inline objects of kind "equation".
// AGENT-CONTRACT: docs/references-review.md#equations lists the supported
// subset; `parse` refuses anything else with ErrorCode::Unsupported (a
// construct outside the subset) or InvalidArgument (a syntax error), each
// with the source offset. tst_rich_text_equations checks both sides.
namespace QindaTK::RichText::Equation {

enum class NodeKind : quint8 {
    Row,            // children in order
    Identifier,     // a variable: italic for letters, upright for capitals of Greek
    Number,
    Operator,       // text: displayed character; `command` its source command, if any
    Function,       // sin, log, lim ... (upright)
    LargeOperator,  // sum, prod, int ... (limits above and below except integrals)
    Text,           // \text{...}
    Space,          // \, \; \quad ...
    Fraction,       // children: numerator row, denominator row
    Root,           // children: radicand row [, index row]
    Scripts,        // children: base [, subscript] [, superscript] (see hasSub/hasSup)
    Fenced,         // text: open delimiter, close: close delimiter, children: content row
};

struct Node {
    NodeKind kind = NodeKind::Row;
    QString text;
    QString command;            // source command name without '\' ("alpha"), or ""
    QString close;              // Fenced only
    bool hasSub = false;        // Scripts only
    bool hasSup = false;
    std::vector<Node> children;
    friend bool operator==(const Node &, const Node &) = default;
};

struct ParseError {
    ErrorCode code = ErrorCode::None;
    QString message;
    int position = -1;          // UTF-16 offset in the source
};

struct Parsed {
    std::optional<Node> root;   // a Row; set when `error.code` is None
    ParseError error;
};

// Sources are at most 4096 UTF-16 units and nest at most 32 groups deep.
inline constexpr int MaximumSourceLength = 4096;
inline constexpr int MaximumDepth = 32;

Parsed parse(const QString &source);
inline bool isCommandLetter(QChar c) { return c.isLetter() && c.unicode() < 128; }
QString displayDelimiter(const QString &source); // "\\langle" -> "⟨", "." -> ""
QString displayText(const Node &node);           // what an atom shows ("*" -> "∗")
QString canonicalSource(const Node &root);   // parse(canonicalSource(n)) == n
QString linearText(const Node &root);        // readable alternative, e.g. "(a + b)/2"
QString structure(const Node &node);         // compact AST dump for tests and diagnostics

struct Symbol {
    QString id;
    QString label;
    QString source;
};
const QVector<Symbol> &palette();
// `source` with the symbol's source inserted at `cursor` (clamped).
QString withSymbol(const QString &source, int cursor, const QString &symbolId);

// Box layout in points, baseline at y = 0, y growing downwards.
struct Glyph {
    QString text;
    QPointF origin;             // left end of the baseline
    double sizePt = 0;
    bool italic = false;
    friend bool operator==(const Glyph &, const Glyph &) = default;
};

// A caret stop of an editing layout: position `index` of the row at
// `row` (child indices from the root), with the row's vertical extent.
struct Stop {
    QVector<int> row;
    int index = 0;
    double x = 0;
    double top = 0;
    double bottom = 0;
};

struct Box {
    double width = 0;
    double ascent = 0;
    double descent = 0;
    QVector<Glyph> glyphs;      // in drawing order
    QVector<QRectF> rules;      // fraction bars and radical overbars
    QVector<QPolygonF> strokes; // radical signs
    double strokeWidth = 0;
    QVector<Stop> stops;          // editing layouts only
    QVector<QRectF> placeholders; // editing layouts only: empty slots
    double height() const { return ascent + descent; }
};

// Families tried in order for every glyph (a math font where installed).
QStringList mathFamilies();
// `editing` also records caret stops and gives empty rows a placeholder
// box (EquationCanvas); `root` is then a normalized tree (Editor).
Box layout(const Node &root, double sizePt, bool editing = false);
// Paints `box` scaled into `rect`, glyph outlines filled with `ink`.
void paint(QPainter *painter, const QRectF &rect, const Box &box, const QColor &ink);
// Parses, lays out (cached) and paints an equation object; false when its
// source does not parse, so the caller can draw a placeholder instead.
bool paintObject(QPainter *painter, const QRectF &rect, const RichObject &object, const QColor &ink);

} // namespace QindaTK::RichText::Equation
