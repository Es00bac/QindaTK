// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_review.h"
#include "rich_text_types.h"
#include "rich_text_publishing.h"
#include <QString>
#include <optional>
#include <variant>

// The primitive steps of RichTextDocument transactions (split out of
// rich_text_document.h to keep that header within the file-size rule).
// AGENT-CONTRACT: RichTextDocument::applyStep handles every alternative of
// `Step`; a new step needs its apply function, its inverse and a cost.
namespace QindaTK::RichText {

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

} // namespace QindaTK::RichText
