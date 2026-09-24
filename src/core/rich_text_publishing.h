// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_types.h"
#include <QHash>
#include <QSet>
#include <QStringList>
#include <optional>

// Publishing structure of the rich-text model (WP-051): named styles with
// inheritance, linked text frames and the merged-cell grid of a table.
// AGENT-CONTRACT: docs/publishing.md (#styles, #merged-cells, #text-frames)
// documents these rules; the codec (rich_text_codec_publishing.cpp) and the
// document operations enforce them through the validators declared here.
namespace QindaTK::RichText {

enum class StyleKind : quint8 { Paragraph, Character, Table };

// Properties of a table style. Unset members inherit from the parent style,
// then from the table's own border and padding.
struct TableStyle {
    std::optional<double> borderPt;
    std::optional<double> paddingPt;
    std::optional<QColor> headerBackground;
    std::optional<QColor> bandBackground;  // every second body row
    bool isEmpty() const { return !borderPt && !paddingPt && !headerBackground && !bandBackground; }
    void merge(const TableStyle &later);   // later's set members win
    friend bool operator==(const TableStyle &, const TableStyle &) = default;
};

// A named style. Paragraph styles carry StyleProperty paragraph members and
// character members; character styles carry character members; table styles
// carry table members. A parent has the same kind; parents never cycle.
struct NamedStyle {
    QString id;
    QString name;
    StyleKind kind = StyleKind::Paragraph;
    QString parentId;
    CharFormatPatch character;
    ParagraphFormatPatch paragraph;
    TableStyle table;
    friend bool operator==(const NamedStyle &, const NamedStyle &) = default;
};
using StyleSheet = QHash<QString, NamedStyle>;

inline constexpr int MaximumStyles = 4096;

QString styleKindName(StyleKind kind);
std::optional<StyleKind> styleKindFromName(const QString &name);
// Root-first chain ending at `id`; empty when `id` is unknown or its parents cycle.
QStringList styleChain(const StyleSheet &styles, const QString &id);
// `id` and every style that inherits from it, directly or indirectly.
QSet<QString> styleFamily(const StyleSheet &styles, const QString &id);
CharFormatPatch resolvedCharacter(const StyleSheet &styles, const QString &id);
ParagraphFormatPatch resolvedParagraph(const StyleSheet &styles, const QString &id);
TableStyle resolvedTable(const StyleSheet &styles, const QString &id);
// `direct` with each inherited (unset) attribute taken from `style`.
CharFormat inheritCharacter(CharFormat direct, const CharFormatPatch &style);
// A run's effective attributes before heading and document defaults: its
// own, then its character style's, then its paragraph style's.
CharFormat effectiveCharFormat(const StyleSheet &styles, const CharFormat &direct,
                               const ParagraphFormat &paragraph);
// A styled paragraph's format: the style's resolved values (defaults where
// the style sets none) except the members marked in `format.overrides`.
// Lists, page breaks and unstyled or unknown-style formats are unchanged.
ParagraphFormat materializedFormat(const StyleSheet &styles, const ParagraphFormat &format);
// Why one style is invalid on its own (name, forbidden or out-of-range members).
QString styleProblem(const NamedStyle &style);
// Why a style sheet is invalid (a style, duplicate names, parents), or "".
QString styleSheetProblem(const StyleSheet &styles);

// A text frame on a layout page. Geometry is in points from the page's
// top-left corner; the story flows through each frame's columns in order.
struct TextFrame {
    QString id;
    int page = 0;                // 0-based page index
    double xPt = 0.0;
    double yPt = 0.0;
    double widthPt = 144.0;
    double heightPt = 144.0;
    int columns = 1;             // 1..24
    double gutterPt = 12.0;      // gap between columns
    double insetPt = 0.0;        // text inset on every side
    QString next;                // next frame of the thread; empty ends it
    friend bool operator==(const TextFrame &, const TextFrame &) = default;
};

// Every frame of a document. The document is one story: it starts in
// `storyStart` and follows `next` links; other threads stay empty.
struct FrameSet {
    QVector<TextFrame> frames;   // creation order
    QString storyStart;          // a thread head; empty: the story is not placed
    const TextFrame *frame(const QString &id) const;
    int indexOf(const QString &id) const;
    QString previous(const QString &id) const;
    QString threadHead(const QString &id) const;
    QStringList storyChain() const;
    int pageCount() const;       // highest frame page + 1
    friend bool operator==(const FrameSet &, const FrameSet &) = default;
};

inline constexpr int MaximumFrames = 1000;
inline constexpr int MaximumFramePages = 1000;

// Why a frame set is invalid (duplicate IDs, geometry, cycles, a thread that
// goes back a page, a frame threaded from two frames), or "".
QString frameSetProblem(const FrameSet &frames);

// Slot -> anchor map of a table grid: `anchor[row * columns + column]` is
// the slot index of the cell that covers it (itself for an anchor).
struct TableGridMap {
    int rows = 0;
    int columns = 0;
    QVector<int> anchor;
    int anchorOf(int row, int column) const { return anchor.at(row * columns + column); }
    bool covered(int row, int column) const { return anchorOf(row, column) != row * columns + column; }
};

// Maps `table`'s merged cells and returns why its spans are invalid ("" when
// valid): spans that overlap, leave the grid or cross the header boundary,
// covered slots that hold paragraphs or spans, anchors without paragraphs.
// The map is complete either way; invalid slots anchor themselves.
QString mapTableGrid(const Table &table, TableGridMap *map);

} // namespace QindaTK::RichText
