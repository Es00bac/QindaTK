// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QString>
#include <QUuid>
#include <QVector>
#include <memory>
#include <optional>

// Value types of the structured rich-text document (WP-050, WP-051). Lengths
// are typographic points; layout converts them to 96-dpi pixels (PxPerPt).
// AGENT-CONTRACT: docs/rich-text.md and docs/publishing.md document these
// fields and the qindatk.richtext persistence schema in rich_text_codec*.cpp;
// a new field needs both, plus a codec round-trip case (tst_rich_text_model,
// or tst_rich_text_styles/_merged_cells/_frames for publishing structure).
namespace QindaTK::RichText {

inline constexpr double PxPerPt = 96.0 / 72.0;
inline constexpr char16_t ObjectChar = 0xFFFC;    // anchors an inline or anchored object
inline constexpr char16_t LineBreakChar = 0x2028; // soft line break inside a paragraph

enum class Alignment : quint8 { Start, End, Left, Right, Center, Justify };
enum class Direction : quint8 { Auto, LeftToRight, RightToLeft };
enum class VerticalAlign : quint8 { Normal, Superscript, Subscript };
enum class Placement : quint8 { Inline, FloatStart, FloatEnd, Page };
enum class ListKind : quint8 { Disc, Circle, Square, Decimal, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman };
enum class Affinity : quint8 { Downstream, Upstream };

// Character attributes. Empty/zero/-1/invalid means "inherit": from the
// run's character style, then the paragraph's style, then the paragraph's
// heading defaults and the document defaults (docs/publishing.md#styles).
struct CharFormat {
    QString family;
    double sizePt = 0.0;
    int weight = 0;        // CSS weight 100..900
    qint8 italic = -1;     // -1 inherit, 0 off, 1 on
    qint8 underline = -1;
    qint8 strike = -1;
    QColor color;
    QColor background;
    QString href;
    VerticalAlign verticalAlign = VerticalAlign::Normal;
    QString objectId;      // set only on the U+FFFC character of an object
    QString styleId;       // named character style (WP-051); empty: none
    friend bool operator==(const CharFormat &, const CharFormat &) = default;
};
size_t qHash(const CharFormat &format, size_t seed = 0) noexcept;

// A partial update. Unset members leave the existing value alone.
struct CharFormatPatch {
    std::optional<QString> family;
    std::optional<double> sizePt;
    std::optional<int> weight;
    std::optional<qint8> italic;
    std::optional<qint8> underline;
    std::optional<qint8> strike;
    std::optional<QColor> color;
    std::optional<QColor> background;
    std::optional<QString> href;
    std::optional<VerticalAlign> verticalAlign;
    std::optional<QString> styleId;
    bool isEmpty() const;
    CharFormat applied(CharFormat format) const;
    void merge(const CharFormatPatch &later); // later's set members win
    friend bool operator==(const CharFormatPatch &, const CharFormatPatch &) = default;
};

// Paragraph properties a named paragraph style controls. A styled
// paragraph's ParagraphFormat holds the resolved values; `overrides` marks
// the ones set directly, which a later style change leaves alone.
// AGENT-CONTRACT: Codec::stylePropertyNames() lists them in bit order.
enum StyleProperty : quint32 {
    StyleAlignment = 1u << 0,
    StyleDirection = 1u << 1,
    StyleIndentStart = 1u << 2,
    StyleIndentEnd = 1u << 3,
    StyleFirstLineIndent = 1u << 4,
    StyleSpaceBefore = 1u << 5,
    StyleSpaceAfter = 1u << 6,
    StyleLineHeight = 1u << 7,
    StyleLineHeightPt = 1u << 8,
    StyleHeadingLevel = 1u << 9,
    StyleKeepWithNext = 1u << 10,
    StyleKeepTogether = 1u << 11,
};
inline constexpr quint32 AllStyleProperties = (1u << 12) - 1;

struct ParagraphFormat {
    Alignment alignment = Alignment::Start;
    Direction direction = Direction::Auto;
    double indentStartPt = 0.0;
    double indentEndPt = 0.0;
    double firstLineIndentPt = 0.0;
    double spaceBeforePt = 0.0;
    double spaceAfterPt = 0.0;
    double lineHeight = 1.0;   // multiple of the natural line height
    double lineHeightPt = 0.0; // exact line height when > 0
    int headingLevel = 0;      // 0 body text, 1..6 outline levels
    QString listId;            // empty: not a list item
    int listLevel = 0;         // 0..8
    bool keepWithNext = false;
    bool keepTogether = false;
    bool pageBreakBefore = false;
    QString styleId;           // named paragraph style; empty: unstyled
    quint32 overrides = 0;     // StyleProperty bits set directly (styled paragraphs only)
    friend bool operator==(const ParagraphFormat &, const ParagraphFormat &) = default;
};

struct ParagraphFormatPatch {
    std::optional<Alignment> alignment;
    std::optional<Direction> direction;
    std::optional<double> indentStartPt;
    std::optional<double> indentEndPt;
    std::optional<double> firstLineIndentPt;
    std::optional<double> spaceBeforePt;
    std::optional<double> spaceAfterPt;
    std::optional<double> lineHeight;
    std::optional<double> lineHeightPt;
    std::optional<int> headingLevel;
    std::optional<QString> listId;
    std::optional<int> listLevel;
    std::optional<bool> keepWithNext;
    std::optional<bool> keepTogether;
    std::optional<bool> pageBreakBefore;
    std::optional<QString> styleId;
    bool isEmpty() const;
    // Applies the set members. On a styled result the patched StyleProperty
    // members become direct overrides; an unstyled result has none.
    ParagraphFormat applied(ParagraphFormat format) const;
    quint32 styleProperties() const; // StyleProperty bits of the set members
    friend bool operator==(const ParagraphFormatPatch &, const ParagraphFormatPatch &) = default;
};

// A run of characters sharing one interned character format.
struct Span {
    int length = 0;
    int format = 0;
    friend bool operator==(const Span &, const Span &) = default;
};

struct Table;

struct Paragraph {
    QUuid id;
    QString text;
    QVector<Span> spans;   // lengths sum to text.size(); adjacent formats differ
    int markFormat = 0;    // typing format of an empty paragraph / its mark
    ParagraphFormat format;
    quint64 revision = 0;  // bumped on every content or format change
    // Derived bookkeeping, maintained by RichTextDocument:
    int flatIndex = -1;    // document order among all paragraphs
    int blockIndex = -1;   // index in the body when not inside a table
    Table *table = nullptr;
    int row = -1;
    int cell = -1;
};
using ParagraphPtr = std::shared_ptr<Paragraph>;

// A grid slot. A merged cell is its top-left slot (the anchor) with spans
// > 1; the other slots it covers stay in the grid with their IDs but hold
// no paragraphs, so splitting restores them (docs/publishing.md#merged-cells).
struct TableCell {
    QUuid id;
    QVector<ParagraphPtr> paragraphs; // empty exactly when a merged cell covers the slot
    QColor background;
    int rowSpan = 1;                  // anchors only; covered slots keep 1
    int columnSpan = 1;
};

struct TableRow {
    QUuid id;
    QVector<TableCell> cells;         // one slot per column
    double minHeightPt = 0.0;
};

struct Table {
    QUuid id;
    QVector<double> columnWeights;    // relative widths, all > 0
    QVector<QUuid> columnIds;         // one per weight; stable across insertion/deletion
    QVector<TableRow> rows;
    int headerRows = 0;               // repeated at the top of continuation pages
    double borderPt = 0.75;           // a table style's values take precedence
    double cellPaddingPt = 4.0;
    QString styleId;                  // named table style; empty: none
    quint64 revision = 0;
    int blockIndex = -1;              // derived
};
using TablePtr = std::shared_ptr<Table>;

// Column identity for a table that has none stored (native files written
// before `columnIds`, hand-built content): deterministic per table and index.
inline QUuid derivedColumnId(const QUuid &table, int column)
{
    return QUuid::createUuidV5(table, QByteArray::number(column));
}

struct Block {
    ParagraphPtr paragraph;
    TablePtr table;
    bool isTable() const { return bool(table); }
    QUuid id() const { return table ? table->id : paragraph->id; }
};

struct ListLevel {
    ListKind kind = ListKind::Disc;
    int start = 1;
    double indentPt = 18.0;
    friend bool operator==(const ListLevel &, const ListLevel &) = default;
};

struct ListDefinition {
    QString id;
    QVector<ListLevel> levels;        // 1..9 levels
    friend bool operator==(const ListDefinition &, const ListDefinition &) = default;
};

// Inline objects occupy their width in the line. FloatStart/FloatEnd sit at
// the start/end edge from their anchor paragraph's top and narrow the lines
// beside them; Page objects sit at offsetX/offsetY in the content box of the
// page where the anchor paragraph starts. The U+FFFC anchor moves with text.
struct RichObject {
    QString id;
    QString kind = QStringLiteral("image");
    Placement placement = Placement::Inline;
    double widthPt = 72.0;
    double heightPt = 72.0;
    double offsetXPt = 0.0;
    double offsetYPt = 0.0;
    double gapPt = 6.0;
    QString source;
    QString altText;
    friend bool operator==(const RichObject &, const RichObject &) = default;
};

struct PageSetup {
    double widthPt = 595.276;         // A4
    double heightPt = 841.89;
    double marginTopPt = 72.0;
    double marginRightPt = 72.0;
    double marginBottomPt = 72.0;
    double marginLeftPt = 72.0;
    int columns = 1;                  // text columns of the content box, 1..24
    double columnGapPt = 18.0;
    friend bool operator==(const PageSetup &, const PageSetup &) = default;
    double contentWidthPt() const { return widthPt - marginLeftPt - marginRightPt; }
    double contentHeightPt() const { return heightPt - marginTopPt - marginBottomPt; }
    double columnWidthPt() const
    { return (contentWidthPt() - (columns - 1) * columnGapPt) / qMax(1, columns); }
};

struct DocumentDefaults {
    QString family = QStringLiteral("sans-serif");
    double sizePt = 11.0;
    QColor color;                     // invalid: the view's ink colour
    friend bool operator==(const DocumentDefaults &, const DocumentDefaults &) = default;
};

// A logical position: paragraph identity plus a UTF-16 offset into its text.
// Affinity only chooses the caret line at a soft wrap or bidi boundary.
struct Position {
    QUuid paragraph;
    int offset = 0;
    Affinity affinity = Affinity::Downstream;
    bool isValid() const { return !paragraph.isNull() && offset >= 0; }
    friend bool operator==(const Position &a, const Position &b)
    { return a.paragraph == b.paragraph && a.offset == b.offset; }
};

struct Selection {
    Position anchor;
    Position focus;
    bool isCollapsed() const { return anchor == focus; }
};

// Where a block list lives: the document body, or one table cell.
struct ContainerSpec {
    QUuid table;
    QUuid row;
    QUuid cell;
    bool isBody() const { return table.isNull(); }
    friend bool operator==(const ContainerSpec &, const ContainerSpec &) = default;
};

QString listMarkerText(ListKind kind, int number);
bool isListKindOrdered(ListKind kind);

} // namespace QindaTK::RichText
