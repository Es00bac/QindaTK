// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"

// Private helpers of the reference fields (rich_text_fields*.cpp): where
// each field's result text sits and what it should read.
// AGENT-CONTRACT: docs/references-review.md#result-text states the rules
// `results` implements; tst_rich_text_references checks each of them.
namespace QindaTK::RichText::Fields {

struct Placement {
    const Paragraph *paragraph = nullptr;
    int offset = 0;
    int length = 0;
};

struct Scan {
    QHash<QString, Placement> placed;   // the first run of each known field
    QStringList order;                  // placed field IDs in document order
    QVector<Placement> duplicates;      // later runs of an already placed field
};

struct Result {
    QString text;
    QString error;                      // why a reference is broken
    int number = 0;                     // sequence or citation number
    bool broken = false;
};

Scan scan(const RichTextDocument &doc);
QHash<QString, Result> results(const RichTextDocument &doc, const Scan &scan);
// QML type names in FieldKind order: caption, cross-reference, citation, bibliography.
const QStringList &typeNames();
std::optional<FieldKind> kindFromType(const QString &type);
// Sets the target members of `field` for its kind, or says why `targetId` does not fit.
QString targetProblem(const RichTextDocument &doc, ReferenceField *field, const QString &targetId);
QString targetIdOf(const ReferenceField &field); // "sequence:<name>" for a caption
QString sourceLabel(const ReferenceSource &source);

} // namespace QindaTK::RichText::Fields
