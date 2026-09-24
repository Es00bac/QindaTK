// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTextLayout>
#include <memory>
#include <unordered_map>

class QPainter;

namespace QindaTK {
class RichTextObjectRenderer;

namespace RichText {
class FlowBuilder;

// One line box of a laid-out paragraph, in paragraph coordinates (y = 0 at
// the top of the first line, x = 0 at the content-box start edge).
struct LineBox {
    int start = 0;          // UTF-16 offset in the layout text (includes preedit)
    int length = 0;
    qreal y = 0;
    qreal height = 0;
    qreal ascent = 0;       // baseline = y + half leading + ascent
    qreal x = 0;            // available line box
    qreal width = 0;
};

// Float exclusion in content-box coordinates, relative to a paragraph top.
struct Exclusion {
    qreal top = 0;
    qreal bottom = 0;
    qreal left = 0;
    qreal right = 0;
    friend bool operator==(const Exclusion &, const Exclusion &) = default;
};

struct ParagraphMetrics {
    quint64 revision = 0;
    qreal width = 0;
    QVector<Exclusion> exclusions;
    QVector<LineBox> lines;
    qreal height = 0;           // sum of line boxes
    qreal spaceBefore = 0;
    qreal spaceAfter = 0;
    qreal indentStart = 0;      // px, list indent included
    bool rightToLeft = false;
    bool keepWithNext = false;
    int preeditOffset = -1;     // model offset where preedit text is shown
    int preeditLength = 0;
    std::shared_ptr<QTextLayout> shaped; // dropped by the LRU; metrics stay
    quint64 lastUse = 0;
};

// A piece of flow content placed on a page, in page content-box coordinates.
struct PageItem {
    enum Kind : quint8 { Lines, TableRows, Float, PageObject };
    Kind kind = Lines;
    QUuid block;                // paragraph (Lines, Float, PageObject) or table
    int first = 0;              // first line or row
    int count = 0;
    qreal x = 0;
    qreal y = 0;
    qreal width = 0;
    qreal height = 0;
    bool headerRepeat = false;  // TableRows: repeated header rows
    QVector<int> cellFirstLine; // TableRows: a split row's per-cell first line
    qreal rowClip = 0;          // TableRows: visible height of a split row (0 = whole)
    QString objectId;
    QVector<Exclusion> exclusions; // Lines: context the paragraph was laid out in
};

struct FlowState {
    QUuid blockId;
    int block = 0;
    int line = 0;               // next line of a paragraph
    int row = 0;                // next row of a table
    QVector<int> cellLine;      // continuation of a split row
    QVector<Exclusion> exclusions; // a continued paragraph keeps its first page's context
    friend bool operator==(const FlowState &, const FlowState &) = default;
};

struct Page {
    QUuid key;                  // stable: section identity + ordinal
    QUuid section;
    int ordinal = 0;
    quint64 revision = 0;       // bumps when the page's content changes
    size_t contentHash = 0;
    FlowState start;
    QVector<PageItem> items;
    Position first;             // first logical position on the page
    Position last;              // last logical position on the page
    bool overflow = false;      // an unbreakable item taller than the page
};

// A laid-out line as it sits on one page (body paragraph or table cell).
struct PageLine {
    const Paragraph *paragraph = nullptr;
    ParagraphMetrics *metrics = nullptr;
    int line = 0;
    QRectF box;                 // line box, page coordinates
    QPointF origin;             // page coordinates of the paragraph layout origin
    int page = -1;
    bool inTable = false;
};

struct CaretBox {
    int page = -1;              // -1 when the position is not laid out
    QRectF rect;                // page (or flow) coordinates
    bool rightToLeft = false;
};

struct PaintContext {
    Selection selection;
    bool hasSelection = false;
    bool showCaret = false;
    Position caret;
    QColor ink;
    QColor link;
    QColor selectionBackground;
    QColor selectionText;
    QColor caretColor;
    QColor border;
    QColor objectFill;
    QColor objectText;
    QColor marker;
    qreal caretWidth = 1.0;
    RichTextObjectRenderer *objects = nullptr;
};

struct LayoutStats {
    quint64 paragraphsShaped = 0;   // QTextLayout constructions since reset
    quint64 pagesPaginated = 0;     // pages rebuilt by the flow since reset
    quint64 pagesReused = 0;        // pages copied at a convergence point
    qint64 lastRepaginationNs = 0;
    int lastRestartPage = -1;
    int lastConvergedPage = -1;     // -1: ran to the end or the frontier
    int shapedLayoutsCached = 0;
    int metricsCached = 0;
};

} // namespace RichText

// Per-view layout of a RichTextDocument: shaping (QTextLayout per paragraph),
// continuous flow or pagination, logical<->page mapping and painting.
// AGENT-CONTRACT (D-050, docs/rich-text.md#layout): the document stays
// authoritative; two views keep two layouts. After an edit, only dirty
// paragraphs are re-shaped and pages are rebuilt from the page before the
// first dirty block until a page starts in the same flow state as before.
class RichTextLayout : public QObject {
    Q_OBJECT
public:
    enum class Mode { Continuous, Paged };
    static constexpr int MaximumShapedLayouts = 2048;

    explicit RichTextLayout(QObject *parent = nullptr);
    ~RichTextLayout() override;

    RichTextDocument *document() const { return m_document; }
    void setDocument(RichTextDocument *document);
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    qreal flowWidth() const { return m_flowWidth; }
    void setFlowWidth(qreal width);   // continuous mode content width (px)
    // View-local input-method composition shown inside `at`'s paragraph;
    // `cursor` is the caret offset inside the composition (-1: at its end).
    void setPreedit(const RichText::Position &at, const QString &text,
                    const QList<QTextLayout::FormatRange> &formats, int cursor = -1);
    bool hasPreedit() const { return !m_preeditText.isEmpty(); }

    // Geometry (px at 96 dpi; one page is one "flow" in continuous mode).
    QSizeF pageSize() const;
    QRectF contentRect() const;       // content box inside a page
    int pageCount() const { return int(m_pages.size()); }
    bool paginationComplete() const { return m_complete; }
    int estimatedPageCount() const;
    const RichText::Page &page(int index) const { return m_pages.at(index); }
    qreal flowHeight() const;         // continuous mode
    bool isLaidOut(const RichText::Position &position) const; // without paginating
    void ensurePaginated(int pageIndex);
    void ensurePaginatedThrough(const RichText::Position &position);
    void paginateAll();
    bool paginateStep(qint64 budgetNs); // true while work remains

    // Mapping.
    RichText::CaretBox caretBox(const RichText::Position &position);
    RichText::Position hitTest(int page, const QPointF &point);
    int pageOfPosition(const RichText::Position &position);
    QVector<QRectF> selectionRects(int page, const RichText::Selection &selection);
    std::optional<QRectF> objectRect(const QString &objectId, int *page);
    const RichText::ParagraphMetrics *metrics(const QUuid &paragraph);
    QString listMarker(const QUuid &paragraph) const; // current even before pagination catches up

    // Navigation that needs shaping (bidi, lines).
    RichText::Position visualMove(const RichText::Position &position, bool right);
    RichText::Position lineMove(const RichText::Position &position, int direction, qreal *goalX);
    RichText::Position lineEdge(const RichText::Position &position, bool end);

    // Painting (page or flow coordinates; the caller sets the transform).
    void paintPage(QPainter *painter, int page, const RichText::PaintContext &context,
                   const QRectF &clip = {});

    const RichText::LayoutStats &stats() const { return m_stats; }
    void resetStats();
    QStringList missingFontFamilies() const;

    // Paragraph shaping; public for tests and table/cell layout helpers.
    RichText::ParagraphMetrics &ensureParagraph(const RichText::Paragraph &p, qreal width,
                                                const QVector<RichText::Exclusion> &exclusions = {},
                                                bool needShape = false);
    QTextLayout *shaped(const RichText::Paragraph &p, RichText::ParagraphMetrics &metrics);
    int layoutOffset(const RichText::ParagraphMetrics &metrics, int modelOffset) const;
    int modelOffset(const RichText::ParagraphMetrics &metrics, int layoutOffset) const;

signals:
    void pagesChanged(int firstChangedPage, int pageCount);
    void layoutReset();

private:
    friend class RichText::FlowBuilder;
    struct TableGeometry;
    void onContentChanged(const RichText::ChangeSet &change);
    void invalidateAll();
    void repaginateFrom(int blockIndex, int lastDirtyBlock);
    void continuePagination(qint64 budgetNs, int stopPage);
    void finalizePages(int firstChanged, int reusedFrom = -1);
    bool rebuildMarkers() const;
    void pageRange(RichText::Page &page);
    QVector<int> pagesContaining(const RichText::Paragraph *p) const;
    void paintLines(QPainter *painter, const RichText::PageItem &item, const RichText::PaintContext &context);
    void paintTable(QPainter *painter, const RichText::PageItem &item, const RichText::PaintContext &context);
    void paintParagraph(QPainter *painter, const RichText::Paragraph &p, RichText::ParagraphMetrics &m,
                        const QPointF &origin, const QRectF &clip, int firstLine, int lineCount,
                        const RichText::PaintContext &context);
    QList<QTextLayout::FormatRange> decorations(const RichText::Paragraph &p, const RichText::ParagraphMetrics &m,
                                                const RichText::PaintContext &context) const;
    void touchLru(RichText::ParagraphMetrics &metrics);
    QTextLayout *buildLayout(const RichText::Paragraph &p, RichText::ParagraphMetrics &metrics);
    const TableGeometry &tableGeometry(const RichText::Table &table);
    qreal itemWidth() const;
    QVector<RichText::PageLine> pageLines(int page);
    std::optional<RichText::PageLine> lineOf(const RichText::Position &position, int *layoutOffset = nullptr);
    RichText::Position positionInLine(const RichText::PageLine &line, qreal x);
    QVector<QUuid> blockIds() const;

    QPointer<RichTextDocument> m_document;
    Mode m_mode = Mode::Paged;
    qreal m_flowWidth = 480;
    struct UuidHash { size_t operator()(const QUuid &id) const noexcept { return qHash(id); } };
    struct StringHash { size_t operator()(const QString &key) const noexcept { return qHash(key); } };
    // AGENT-GUARD: node-based maps keep ParagraphMetrics references valid
    // while the flow shapes further paragraphs; a QHash could rehash under it.
    std::unordered_map<QUuid, RichText::ParagraphMetrics, UuidHash> m_metrics;          // default context
    std::unordered_map<QString, RichText::ParagraphMetrics, StringHash> m_contextMetrics; // exclusions/preedit
    QHash<QUuid, std::shared_ptr<TableGeometry>> m_tables;
    QVector<RichText::Page> m_pages;
    QHash<QUuid, QPair<size_t, quint64>> m_revisions; // page key -> (content hash, revision)
    qreal m_flowHeight = 0;
    RichText::FlowState m_frontier;   // where progressive pagination resumes
    bool m_complete = false;
    QVector<QUuid> m_paginatedBlocks; // block IDs the current pages were built from
    // Markers derive from the document alone, so a query rebuilds them
    // lazily; m_markersChanged tells finalizePages to re-hash every page.
    mutable QHash<QUuid, QString> m_markers;
    mutable bool m_markersDirty = true;
    mutable bool m_markersChanged = false;
    RichText::Position m_preeditAt;
    QString m_preeditText;
    QList<QTextLayout::FormatRange> m_preeditFormats;
    int m_preeditCursor = -1;
    quint64 m_useCounter = 0;
    quint64 m_revisionCounter = 0;
    int m_shapedCount = 0;
    RichText::LayoutStats m_stats;
};

} // namespace QindaTK
