// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_types.h"
#include <QHash>
#include <QString>
#include <QVector>
#include <optional>
#include <variant>

// Semantic annotations of the rich-text model (WP-052): reference fields
// and bibliography sources, comment threads and tracked changes. Text refers
// to them through character-format marks (CharFormat::fieldId, commentIds,
// insertionId, deletionId), so every text edit, undo and paste carries them
// with the characters; these records hold what the marks mean.
// AGENT-CONTRACT: docs/references-review.md documents the records, the marks,
// the result-text rules below and their qindatk.richtext version-2 members
// (rich_text_codec_review.cpp, RichTextSessionAdapter::liveJson).
namespace QindaTK::RichText {

// Footnote and Endnote fields carry their note text; pagination sets
// footnotes at the foot of the page (or column) of their reference and
// endnotes after the last block (docs/references-review.md#notes).
enum class FieldKind : quint8 { Sequence, CrossReference, Citation, Bibliography, Footnote, Endnote };

// A generated text run. Its result text is recomputed inside every
// transaction that commits (RichTextDocument::reconcileFields), so numbers
// change in the same undo step as the edit that moved them.
struct ReferenceField {
    QString id;
    FieldKind kind = FieldKind::Sequence;
    QString sequence;   // Sequence: the numbered series ("Figure", "Table", "Equation")
    QString target;     // CrossReference: a Sequence field; Citation: a source
    QString label;      // optional name a reference panel shows
    QString text;       // Footnote/Endnote: the note text (plain, at most MaximumNoteText)
    bool isNote() const { return kind == FieldKind::Footnote || kind == FieldKind::Endnote; }
    friend bool operator==(const ReferenceField &, const ReferenceField &) = default;
};

struct ReferenceSource {
    QString id;
    QString author;
    QString title;
    QString year;
    QString publisher;
    friend bool operator==(const ReferenceSource &, const ReferenceSource &) = default;
};

struct CommentReply {
    QString id;
    QString author;
    QString time;       // ISO 8601, UTC
    QString body;
    friend bool operator==(const CommentReply &, const CommentReply &) = default;
};

// A thread anchored on the characters whose format lists its ID. With no
// such characters left the thread is orphaned; it stays, so undo or a paste
// of the cut text anchors it again.
struct CommentThread {
    QString id;
    QString author;
    QString time;
    QString body;
    bool resolved = false;
    QVector<CommentReply> replies;
    friend bool operator==(const CommentThread &, const CommentThread &) = default;
};

// Insertion and Deletion mark characters and paragraph marks (the
// `markFormat` of the paragraph before the break). Format records the look
// text and paragraphs had before a tracked formatting change, so rejecting
// it restores them (docs/references-review.md#tracked-changes).
enum class ChangeKind : quint8 { Insertion, Deletion, Format };

struct ParagraphFormatChange {
    QUuid paragraph;
    ParagraphFormat before;
    friend bool operator==(const ParagraphFormatChange &, const ParagraphFormatChange &) = default;
};

struct TrackedChange {
    QString id;
    ChangeKind kind = ChangeKind::Insertion;
    QString author;
    QString time;
    // Format: the former character formats, indexed by CharFormat::formatSlot
    // (attributes only, no marks), and the former paragraph formats.
    QVector<CharFormat> formats;
    QVector<ParagraphFormatChange> paragraphs;
    // Deletion of table structure: accepting it removes this row or column
    // of `table` after the marked text (docs/references-review.md#tables).
    QUuid table;
    QUuid row;
    QUuid column;
    friend bool operator==(const TrackedChange &, const TrackedChange &) = default;
};

using Annotation = std::variant<ReferenceField, ReferenceSource, CommentThread, TrackedChange>;

// Every record of a document. IDs are unique across the four tables.
struct Annotations {
    QHash<QString, ReferenceField> fields;
    QHash<QString, ReferenceSource> sources;
    QHash<QString, CommentThread> threads;
    QHash<QString, TrackedChange> changes;
    bool isEmpty() const { return fields.isEmpty() && sources.isEmpty() && threads.isEmpty() && changes.isEmpty(); }
    qsizetype size() const { return fields.size() + sources.size() + threads.size() + changes.size(); }
    bool contains(const QString &id) const;
    std::optional<Annotation> find(const QString &id) const;
    void set(const QString &id, const std::optional<Annotation> &value); // nullopt removes
    friend bool operator==(const Annotations &, const Annotations &) = default;
};

inline constexpr int MaximumAnnotations = 100000;
inline constexpr int MaximumNoteText = 4000;
inline constexpr int MaximumFormerFormats = 1024; // per format change

QString annotationId(const Annotation &annotation);
QString fieldKindName(FieldKind kind);             // sequence, crossReference, citation, bibliography, footnote, endnote
std::optional<FieldKind> fieldKindFromName(const QString &name);
QString changeKindName(ChangeKind kind);           // insertion, deletion, format
std::optional<ChangeKind> changeKindFromName(const QString &name);
// A series name: 1-32 letters, digits or spaces, starting with a letter.
bool validSequenceName(const QString &name);

// Result text rules (docs/references-review.md#result-text).
QString sequenceResult(const QString &sequence, int number); // "Figure 2"; "(2)" for Equation
QString brokenReferenceResult();                            // "[?]"
QString citationResult(int number);                         // "[2]"
QString noteResult(FieldKind kind, int number);             // footnote "2", endnote "ii"
QString bibliographyEntry(const ReferenceSource &source, int number);
QString emptyBibliographyResult();

// Why a record set is invalid (empty or duplicate IDs, bad names), or "".
QString annotationsProblem(const Annotations &annotations);

} // namespace QindaTK::RichText
