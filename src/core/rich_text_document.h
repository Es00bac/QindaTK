// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "rich_text_publishing.h"
#include "rich_text_review.h"
#include "rich_text_types.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QtQml/qqmlregistration.h>
#include <variant>

namespace QindaTK {
namespace RichText {

// Primitive, invertible edit steps. A transaction is an ordered list of them.
struct StepText {
    QUuid paragraph;
    int offset = 0;
    QString removed;
    QVector<Span> removedSpans;
    QString inserted;
    QVector<Span> insertedSpans;
    QVector<RichObject> removedObjects;   // objects whose anchor chars leave
    QVector<RichObject> insertedObjects;  // objects whose anchor chars arrive
};
struct StepSpans {
    QUuid paragraph;
    int offset = 0;
    QVector<Span> before;                 // same total length as after
    QVector<Span> after;
    int markBefore = -1;                  // -1: mark format unchanged
    int markAfter = -1;
};
struct StepParagraphFormat {
    QUuid paragraph;
    ParagraphFormat before;
    ParagraphFormat after;
};
struct StepSplit {                        // inverse: merge at offset
    QUuid paragraph;
    int offset = 0;
    ParagraphPtr created;                 // keeps its identity across redo
};
struct StepMerge {                        // inverse: split at joinOffset
    QUuid paragraph;
    ParagraphPtr next;
    int joinOffset = 0;
};
struct StepBlocks {
    ContainerSpec container;
    int index = 0;
    QVector<Block> blocks;
    QVector<RichObject> objects;          // objects anchored inside the blocks
    bool insert = true;
};
struct StepMove {                         // body blocks keep identity and anchors
    int from = 0;
    int count = 0;
    int to = 0;                           // index before the move; inverse swaps
};
struct StepList {
    QString listId;
    std::optional<ListDefinition> before;
    std::optional<ListDefinition> after;
};
struct StepObject {
    QString objectId;
    RichObject before;
    RichObject after;
};
struct StepDocument {
    PageSetup pageBefore;
    PageSetup pageAfter;
    DocumentDefaults defaultsBefore;
    DocumentDefaults defaultsAfter;
};
// Replaces one table's grid (rows, columns, widths, header rows) in place of
// the registered one. AGENT-NOTE: `before` and `after` share the Paragraph
// objects of unchanged cells, so those keep their IDs, revisions and
// anchors. Later edits inside cells mutate the live grid; linear history
// undoes them before this step, so swapping the pointers back is exact.
struct StepTable {
    QUuid tableId;
    TablePtr before;
    TablePtr after;
    QVector<RichObject> removedObjects; // objects in removed rows/columns
};
// Creates (no `before`), replaces or removes (no `after`) one named style.
// Paragraph styles also rewrite their paragraphs' resolved formats with
// separate StepParagraphFormat steps of the same transaction.
struct StepStyle {
    QString styleId;
    std::optional<NamedStyle> before;
    std::optional<NamedStyle> after;
};
struct StepFrames {                       // the whole frame set (frames are few)
    FrameSet before;
    FrameSet after;
};
// Creates (no `before`), replaces or removes (no `after`) one annotation
// record (WP-052). The marks in the text change through StepSpans/StepText.
struct StepAnnotation {
    QString id;
    std::optional<Annotation> before;
    std::optional<Annotation> after;
};
using Step = std::variant<StepText, StepSpans, StepParagraphFormat, StepSplit, StepMerge,
                          StepBlocks, StepMove, StepList, StepObject, StepDocument, StepTable,
                          StepStyle, StepFrames, StepAnnotation>;

struct Transaction {
    QUuid id;
    QString label;
    QString mergeKey;
    QVector<Step> steps;
    Selection before;
    Selection after;
    quint64 bytes = 0;
};

// What one applied transaction, undo or redo touched; views invalidate from it.
struct ChangeSet {
    QSet<QUuid> paragraphs;         // content or format changed, still present
    QSet<QUuid> removedParagraphs;
    QSet<QUuid> tables;             // a table or its cells changed
    QSet<QString> listDefinitions;  // definitions changed in place: their items re-lay
    bool structure = false;         // blocks inserted, removed, split or merged
    bool lists = false;             // numbering may change (definitions or membership)
    bool objects = false;
    bool document = false;          // page setup or defaults
    bool annotations = false;       // fields, sources, comments or tracked changes (records)
    bool reset = false;
    void merge(const ChangeSet &other);
    bool isEmpty() const;
};

// A document-maintained position: every applied step maps it forward.
struct PositionAnchor {
    Position position;
};

// A self-contained copy of a range: formats by value, objects by value.
struct Fragment {
    QVector<Block> blocks;
    QVector<CharFormat> formats;    // spans index this list
    QHash<QString, RichObject> objects;
    QHash<QString, ListDefinition> lists;
    StyleSheet styles;              // the styles the blocks use, with their parents
    bool openStart = true;          // first paragraph joins the insertion point
    bool openEnd = true;            // last paragraph joins the text after it
};

} // namespace RichText

// Authoritative structured text model: paragraphs with interned character
// runs, tables, lists and object anchors, plus transactions and history.
// AGENT-CONTRACT: views (RichTextLayout) never mutate it directly; every
// edit is a transaction of RichText::Step values so undo, external history
// (D-050) and persistence see the same change. GUI thread only.
class RichTextDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString documentId READ documentIdString CONSTANT)
    Q_PROPERTY(QString revision READ revisionString NOTIFY changed)
    Q_PROPERTY(QString resetGeneration READ resetGenerationString NOTIFY changed)
    Q_PROPERTY(int paragraphCount READ paragraphCount NOTIFY changed)
    Q_PROPERTY(int characterCount READ characterCount NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY changed)
    Q_PROPERTY(bool externalUndo READ externalUndo WRITE setExternalUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QVariantMap pageSetup READ pageSetupMap NOTIFY changed)
    Q_PROPERTY(QVariantMap pageSetupMm READ pageSetupMm NOTIFY changed)
    Q_PROPERTY(QString defaultFontFamily READ defaultFontFamily NOTIFY changed)
    Q_PROPERTY(double defaultFontSize READ defaultFontSize NOTIFY changed)
    Q_PROPERTY(QVariantList tables READ tableSnapshots NOTIFY changed)
    Q_PROPERTY(QVariantList styles READ styleSnapshots NOTIFY changed)
    Q_PROPERTY(QStringList styleKinds READ styleKinds CONSTANT)
    Q_PROPERTY(QVariantList frames READ frameSnapshots NOTIFY changed)
    Q_PROPERTY(QString storyFrameId READ storyFrameId NOTIFY changed)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY changed)
    // WP-052 (docs/references-review.md): equations, reference fields and review.
    Q_PROPERTY(QVariantList equations READ equationSnapshots NOTIFY changed)
    Q_PROPERTY(QVariantList equationSymbols READ equationSymbols CONSTANT)
    Q_PROPERTY(QVariantList references READ referenceSnapshots NOTIFY changed)
    Q_PROPERTY(QVariantList referenceTargets READ referenceTargets NOTIFY changed)
    Q_PROPERTY(QStringList referenceTypes READ referenceTypes CONSTANT)
    Q_PROPERTY(QVariantList sources READ sourceSnapshots NOTIFY changed)
    Q_PROPERTY(QVariantList reviewItems READ reviewItems NOTIFY changed)
    Q_PROPERTY(bool trackChanges READ trackChanges WRITE setTrackChanges NOTIFY changed)
    Q_PROPERTY(QString reviewAuthor READ reviewAuthor WRITE setReviewAuthor NOTIFY changed)
public:
    static constexpr int MaximumHistory = 512;
    static constexpr quint64 MaximumHistoryBytes = 64ull * 1024 * 1024;

    explicit RichTextDocument(QObject *parent = nullptr);
    ~RichTextDocument() override;

    // --- identity and state
    DocumentId documentId() const { return m_documentId; }
    QString documentIdString() const { return m_documentId.toString(); }
    quint64 revision() const { return m_revision; }
    QString revisionString() const { return QString::number(m_revision); }
    quint64 resetGenerationValue() const { return m_resetGeneration; }
    QString resetGenerationString() const { return QString::number(m_resetGeneration); }
    SnapshotHeader header() const;
    int paragraphCount() const;
    int characterCount() const;
    bool readOnly() const { return m_readOnly; }
    void setReadOnly(bool readOnly);
    QString lastError() const { return m_lastError; }
    QString lastErrorCode() const { return m_lastErrorCode; } // e.g. "unsupported", "invalidArgument"

    // --- structure (read)
    const QVector<RichText::Block> &blocks() const { return m_blocks; }
    RichText::Paragraph *paragraph(const QUuid &id) const { return m_paragraphs.value(id); }
    RichText::Table *table(const QUuid &id) const { return m_tables.value(id); }
    QVariantList tableSnapshots() const;
    Q_INVOKABLE QVariantMap tableSnapshot(const QString &tableId) const;
    const QVector<RichText::Paragraph *> &paragraphsInOrder() const;
    RichText::Paragraph *nextParagraph(const RichText::Paragraph *p) const;
    RichText::Paragraph *previousParagraph(const RichText::Paragraph *p) const;
    RichText::ContainerSpec containerOf(const RichText::Paragraph *p) const;
    int compare(const RichText::Position &a, const RichText::Position &b) const;
    bool isValidPosition(const RichText::Position &position) const;
    RichText::Position clampPosition(const RichText::Position &position) const;
    RichText::Position startPosition() const;
    RichText::Position endPosition() const;
    RichText::Position paragraphStart(const QUuid &paragraph) const { return {paragraph, 0}; }
    RichText::Position paragraphEnd(const QUuid &paragraph) const;

    // --- formats, lists, objects, page
    const RichText::CharFormat &charFormat(int index) const;
    int internFormat(const RichText::CharFormat &format);
    int formatCount() const { return int(m_formats.size()); }
    int formatIndexAt(const RichText::Position &position) const;
    const QHash<QString, RichText::ListDefinition> &lists() const { return m_lists; }
    const RichText::ListDefinition *list(const QString &id) const;
    const QHash<QString, RichText::RichObject> &objects() const { return m_objects; }
    const RichText::RichObject *object(const QString &id) const;
    RichText::PageSetup pageSetupValue() const { return m_page; }
    RichText::DocumentDefaults defaults() const { return m_defaults; }
    const RichText::StyleSheet &styleSheet() const { return m_styles; }
    const RichText::NamedStyle *style(const QString &id) const;
    // A run's attributes with its character style and its paragraph's style
    // applied (heading and document defaults still inherit); cached per style.
    RichText::CharFormat effectiveCharFormat(const RichText::CharFormat &direct,
                                             const RichText::ParagraphFormat &paragraph) const;
    RichText::TableStyle tableStyle(const RichText::Table &table) const; // resolved style of a table
    const RichText::FrameSet &frameSet() const { return m_frames; }
    QVariantMap pageSetupMap() const;
    QVariantMap pageSetupMm() const;
    QString defaultFontFamily() const { return m_defaults.family; }
    double defaultFontSize() const { return m_defaults.sizePt; }

    // --- transactions (GUI thread; rejected while readOnly)
    bool beginTransaction(const QString &label, const QString &mergeKey = {},
                          const RichText::Selection &before = {});
    OperationError apply(RichText::Step step);
    bool commitTransaction(const RichText::Selection &after = {});
    void cancelTransaction();
    bool inTransaction() const { return m_open.has_value(); }

    // --- editing operations (rich_text_ops.cpp). Each joins an open
    // transaction or wraps itself in one. They return the resulting caret.
    std::optional<RichText::Position> insertText(const RichText::Position &at, const QString &text,
                                                 int format = -1);
    std::optional<RichText::Position> deleteRange(const RichText::Position &a,
                                                  const RichText::Position &b);
    std::optional<RichText::Position> splitParagraph(const RichText::Position &at);
    bool applyCharFormat(const RichText::Position &a, const RichText::Position &b,
                         const RichText::CharFormatPatch &patch);
    bool setParagraphFormat(const RichText::Position &a, const RichText::Position &b,
                            const RichText::ParagraphFormatPatch &patch);
    bool setList(const RichText::Position &a, const RichText::Position &b,
                 std::optional<RichText::ListKind> kind);
    bool changeListLevel(const RichText::Position &a, const RichText::Position &b, int delta);
    std::optional<RichText::Position> insertObject(const RichText::Position &at,
                                                   RichText::RichObject object);
    bool setObject(const RichText::RichObject &object);
    std::optional<RichText::Position> insertTable(const RichText::Position &at, int rows,
                                                  int columns, int headerRows = 0);
    Q_INVOKABLE bool insertTableRow(const QString &tableId, int index);
    Q_INVOKABLE bool removeTableRow(const QString &tableId, const QString &rowId);
    Q_INVOKABLE bool insertTableColumn(const QString &tableId, int index);
    Q_INVOKABLE bool removeTableColumn(const QString &tableId, const QString &columnId);
    Q_INVOKABLE bool setTableColumnWidth(const QString &tableId, const QString &columnId, double width);
    Q_INVOKABLE bool setTableCellText(const QString &tableId, const QString &cellId, const QString &text);
    // Merged cells (rich_text_table_spans.cpp): the cells must tile a
    // rectangle that stays inside or outside the header rows.
    Q_INVOKABLE bool mergeTableCells(const QString &tableId, const QStringList &cellIds);
    Q_INVOKABLE bool splitTableCell(const QString &tableId, const QString &cellId);
    Q_INVOKABLE bool setTableStyle(const QString &tableId, const QString &styleId);
    std::optional<RichText::Position> insertPageBreak(const RichText::Position &at);
    bool setPageSetup(const RichText::PageSetup &page);
    bool setDefaults(const RichText::DocumentDefaults &defaults);
    // Sections are body ranges separated by pageBreakBefore paragraphs; the
    // first section's ID is the null UUID. targetIndex counts sections before
    // the move (0..sectionCount).
    bool moveSection(const QUuid &section, int targetIndex);
    RichText::Fragment extractFragment(const RichText::Position &a, const RichText::Position &b) const;
    std::optional<RichText::Position> insertFragment(const RichText::Position &at,
                                                     const RichText::Fragment &fragment);
    QString plainText(const RichText::Position &a, const RichText::Position &b) const;
    QVector<QUuid> sectionIds() const;
    RichText::ParagraphPtr paragraphPtr(const RichText::Paragraph *p) const;
    QVector<RichText::RichObject> objectsIn(const RichText::Paragraph &p, int from, int to) const;
    QVector<RichText::RichObject> objectsIn(const QVector<RichText::Block> &blocks) const;

    // --- named styles (rich_text_style_edit.cpp); each edit is one transaction
    QVariantList styleSnapshots() const;
    QStringList styleKinds() const;
    Q_INVOKABLE QString createStyle(const QString &name, const QString &kind, const QString &parentId = {});
    // `patch`: name, parentId, and "character", "paragraph" or "table" maps in
    // the persistence keys; a null member clears it (inherits).
    Q_INVOKABLE bool updateStyle(const QString &styleId, const QVariantMap &patch);
    // Paragraph styles: whole paragraphs; character styles: their text; table
    // styles: tables. An empty `styleId` removes the kind's style.
    Q_INVOKABLE bool applyStyle(const QString &styleId, const QStringList &targetIds, const QString &kind = {});
    Q_INVOKABLE QVariantList directOverrides(const QStringList &paragraphIds) const;
    // `keys` like "paragraph.align" or "character.sizePt"; empty: all of them.
    Q_INVOKABLE bool resetDirectFormatting(const QStringList &paragraphIds, const QStringList &keys = {});
    // The style-controlled look of a paragraph, as an updateStyle() patch.
    Q_INVOKABLE QVariantMap stylePatchFromParagraph(const QString &paragraphId) const;
    bool applyParagraphStyle(const RichText::Position &a, const RichText::Position &b, const QString &styleId);
    bool applyCharacterStyle(const RichText::Position &a, const RichText::Position &b, const QString &styleId);

    // --- text frames (rich_text_frames.cpp); geometry maps use millimetres
    QVariantList frameSnapshots() const;
    QString storyFrameId() const { return m_frames.storyStart; }
    Q_INVOKABLE QString createTextFrame(int pageIndex, const QVariantMap &geometryMm);
    Q_INVOKABLE bool setTextFrameGeometry(const QString &frameId, const QVariantMap &geometryMm);
    Q_INVOKABLE bool setTextFrameLayout(const QString &frameId, const QVariantMap &layout);
    Q_INVOKABLE bool connectTextFrames(const QString &sourceId, const QString &targetId);
    Q_INVOKABLE bool disconnectTextFrame(const QString &frameId);
    Q_INVOKABLE bool removeTextFrame(const QString &frameId);
    bool setFrameSet(const RichText::FrameSet &frames, const QString &label);

    // --- semantic annotations (WP-052, docs/references-review.md). Marks in
    // character formats refer to these records; each request is one transaction.
    const RichText::Annotations &annotations() const { return m_annotations; }
    bool trackChanges() const { return m_trackChanges; }
    void setTrackChanges(bool enabled);
    QString reviewAuthor() const { return m_reviewAuthor; }
    void setReviewAuthor(const QString &author);
    // Equations (rich_text_equation_edit.cpp): inline objects of kind "equation"
    // whose `source` is the TeX-subset source and `altText` its linear text.
    QVariantList equationSnapshots() const;
    QVariantList equationSymbols() const;
    Q_INVOKABLE QVariantMap equationSnapshot(const QString &equationId) const;
    Q_INVOKABLE QVariantMap validateEquation(const QString &source) const;
    Q_INVOKABLE QString insertEquation(const QString &paragraphId, int offset, const QString &source);
    Q_INVOKABLE bool setEquationSource(const QString &equationId, const QString &source);
    Q_INVOKABLE QString equationSourceWithSymbol(const QString &source, int cursor, const QString &symbolId) const;
    std::optional<RichText::Position> insertEquationAt(const RichText::Position &at, const QString &source,
                                                       QString *equationId = nullptr);
    // Reference fields (rich_text_fields.cpp): caption, cross-reference, citation, bibliography.
    QVariantList referenceSnapshots() const;
    QVariantList referenceTargets() const;
    QStringList referenceTypes() const;
    QVariantList sourceSnapshots() const;
    Q_INVOKABLE QString insertReference(const QString &paragraphId, int offset, const QString &type,
                                        const QString &targetId);
    Q_INVOKABLE bool editReference(const QString &referenceId, const QVariantMap &patch);
    Q_INVOKABLE bool removeReference(const QString &referenceId);
    Q_INVOKABLE QString addSource(const QVariantMap &source);
    Q_INVOKABLE bool updateSource(const QString &sourceId, const QVariantMap &patch);
    Q_INVOKABLE bool removeSource(const QString &sourceId);
    Q_INVOKABLE bool updateFields();
    std::optional<RichText::Position> insertField(const RichText::Position &at, RichText::ReferenceField field);
    // Review (rich_text_review_edit.cpp): comments and tracked changes.
    QVariantList reviewItems() const;
    Q_INVOKABLE QString addComment(const QString &startParagraphId, int startOffset,
                                   const QString &endParagraphId, int endOffset, const QString &body);
    QString addCommentAt(const RichText::Position &a, const RichText::Position &b, const QString &body);
    Q_INVOKABLE bool replyToComment(const QString &threadId, const QString &body);
    Q_INVOKABLE bool resolveComment(const QString &threadId, bool resolved = true);
    Q_INVOKABLE bool removeComment(const QString &threadId);
    Q_INVOKABLE bool acceptChange(const QString &changeId);
    Q_INVOKABLE bool rejectChange(const QString &changeId);
    Q_INVOKABLE QString reviewContextAt(const QString &paragraphId, int offset) const;
    // The first position at or after `position` that is not tracked-deleted text.
    RichText::Position afterTrackedDeletion(const RichText::Position &position) const;

    // --- content replacement (loading); clears history, bumps reset generation
    void resetContent(QVector<RichText::Block> blocks, QVector<RichText::CharFormat> formats,
                      QHash<QString, RichText::ListDefinition> lists,
                      QHash<QString, RichText::RichObject> objects,
                      const RichText::PageSetup &page, const RichText::DocumentDefaults &defaults,
                      const DocumentId &id = {}, RichText::StyleSheet styles = {},
                      RichText::FrameSet frames = {}, RichText::Annotations annotations = {});
    void clear();

    // --- history
    bool externalUndo() const { return m_externalUndo; }
    void setExternalUndo(bool enabled);
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    std::optional<RichText::Selection> undoTransaction();
    std::optional<RichText::Selection> redoTransaction();
    const RichText::Transaction *lastCommitted() const;

    // --- anchors mapped by every applied step
    void registerAnchor(RichText::PositionAnchor *anchor);
    void unregisterAnchor(RichText::PositionAnchor *anchor);

    // --- QML
    Q_INVOKABLE bool undo() { return undoTransaction().has_value(); }
    Q_INVOKABLE bool redo() { return redoTransaction().has_value(); }
    // AGENT-CONTRACT (D-050): with externalUndo the host records each
    // committed(transactionId) step and replays it here with the document's
    // current revision; a mismatched revision or unknown ID changes nothing.
    Q_INVOKABLE bool applyHistoryChanges(const QString &transactionId, bool reverse,
                                         const QString &expectedRevision);
    Q_INVOKABLE bool releaseHistoryTransaction(const QString &transactionId);
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE bool loadJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);
    Q_INVOKABLE bool exportPdf(const QString &path);
    Q_INVOKABLE QString text() const;
    Q_INVOKABLE QStringList paragraphIds() const;
    Q_INVOKABLE QString paragraphText(const QString &paragraphId) const;
    Q_INVOKABLE QVariantMap paragraphFormatMap(const QString &paragraphId) const;
    Q_INVOKABLE QStringList missingFontFamilies() const;
    Q_INVOKABLE bool applyPageSetupMm(const QVariantMap &settings);

signals:
    void changed();
    void historyChanged();
    void contentChanged(const QindaTK::RichText::ChangeSet &change);
    void committed(const QString &transactionId, const QString &label);
    void historyNavigated(const QindaTK::RichText::Selection &selection, bool undo);
    void rejected(const QString &message);

private:
    friend class RichTextCodec;
    struct Receipt { RichText::Transaction transaction; bool applied = true; quint64 revision = 0; };
    OperationError applyStep(RichText::Step &step, bool forward, RichText::ChangeSet &change);
    OperationError applyText(RichText::StepText &step, bool forward, RichText::ChangeSet &change);
    OperationError applySpans(RichText::StepSpans &step, bool forward, RichText::ChangeSet &change);
    OperationError applySplit(const QUuid &id, int offset, const RichText::ParagraphPtr &created,
                              RichText::ChangeSet &change);
    OperationError applyMerge(const QUuid &id, const RichText::ParagraphPtr &next, int *joinOffset,
                              RichText::ChangeSet &change);
    OperationError applyBlocks(RichText::StepBlocks &step, bool insert, RichText::ChangeSet &change);
    OperationError applyMove(const RichText::StepMove &step, bool forward, RichText::ChangeSet &change);
    OperationError applyTable(const RichText::StepTable &step, bool forward, RichText::ChangeSet &change);
    OperationError applyStyleStep(const RichText::StepStyle &step, bool forward, RichText::ChangeSet &change);
    // AGENT-GUARD: formats may only name existing character styles, or the
    // saved file would not load (the codec refuses unknown style references).
    bool knownCharacterStyles(const QVector<RichText::Span> &spans, int mark = -1) const;
    OperationError applyFrames(const RichText::StepFrames &step, bool forward, RichText::ChangeSet &change);
    bool commitStyle(const QString &label, std::optional<RichText::NamedStyle> after, const QString &styleId);
    OperationError applyAnnotation(const RichText::StepAnnotation &step, bool forward, RichText::ChangeSet &change);
    // Rewrites stale generated field results as steps of the open transaction (commit calls it).
    bool reconcileFields();
    // The insertion change typed text at `at` joins or starts ("" without
    // tracking); nullopt when the record could not be created.
    std::optional<QString> trackedInsertionId(const RichText::Position &at);
    RichText::Position outsideField(const RichText::Position &position) const; // end of a field it is inside
    void expandOverFields(RichText::Position *start, RichText::Position *end) const;
    bool markDeleted(const RichText::Position &start, const RichText::Position &end);
    bool resolveChange(const QString &changeId, bool accept);
    QVector<RichText::Block> *bodyOrNull(const RichText::ContainerSpec &spec);
    QVector<RichText::ParagraphPtr> *cellParagraphs(const RichText::ContainerSpec &spec);
    void registerBlock(const RichText::Block &block);
    void unregisterBlock(const RichText::Block &block);
    void touch(RichText::Paragraph *p, RichText::ChangeSet &change);
    void finishChange(const RichText::ChangeSet &change);
    void ensureOrder() const;
    OperationError reject(ErrorCode code, const QString &message);
    bool revertSteps(QVector<RichText::Step> &steps, qsizetype count, RichText::ChangeSet &change);
    void recordHistory(RichText::Transaction transaction);
    void trimHistory();
    static quint64 cost(const RichText::Transaction &transaction);
    RichText::Position fallbackAfterRemoval(const QVector<RichText::Block> &removed) const;
    void mapAnchorsForText(const QUuid &id, int offset, int removed, int inserted);

    DocumentId m_documentId = DocumentId::create();
    quint64 m_revision = 0;
    quint64 m_resetGeneration = 0;
    quint64 m_contentCounter = 0;
    QVector<RichText::Block> m_blocks;
    QHash<QUuid, RichText::Paragraph *> m_paragraphs;
    QHash<QUuid, RichText::Table *> m_tables;
    QVector<RichText::CharFormat> m_formats;
    QHash<RichText::CharFormat, int> m_formatIndex;
    QHash<QString, RichText::ListDefinition> m_lists;
    QHash<QString, RichText::RichObject> m_objects;
    RichText::PageSetup m_page;
    RichText::DocumentDefaults m_defaults;
    RichText::StyleSheet m_styles;
    RichText::FrameSet m_frames;
    RichText::Annotations m_annotations;
    bool m_trackChanges = false;
    QString m_reviewAuthor;
    QString m_lastErrorCode;
    mutable QHash<QString, RichText::CharFormatPatch> m_resolvedCharacter; // cleared by style steps
    mutable QVector<RichText::Paragraph *> m_flat;
    mutable bool m_orderDirty = true;
    std::optional<RichText::Transaction> m_open;
    RichText::ChangeSet m_openChange;
    QVector<RichText::Transaction> m_history;
    qsizetype m_historyCursor = 0;
    bool m_mergeAllowed = false;
    QHash<QUuid, Receipt> m_receipts;
    QVector<RichText::PositionAnchor *> m_anchors;
    QString m_lastError;
    bool m_readOnly = false;
    bool m_externalUndo = false;
};

} // namespace QindaTK

Q_DECLARE_METATYPE(QindaTK::RichText::ChangeSet)
Q_DECLARE_METATYPE(QindaTK::RichText::Selection)
