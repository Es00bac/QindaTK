// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"

// Private helpers shared by the rich_text_*.cpp operation files.
namespace QindaTK::RichText::Detail {

// Joins an open transaction, or opens one it commits/cancels itself.
struct AutoTransaction {
    AutoTransaction(RichTextDocument *document, const QString &label);
    ~AutoTransaction();
    AutoTransaction(const AutoTransaction &) = delete;
    AutoTransaction &operator=(const AutoTransaction &) = delete;
    // Commits an owned transaction (caret as its after-selection) or cancels
    // it when !ok; a joined transaction is left to its owner.
    bool finish(bool ok, const Position &caret);
    RichTextDocument *doc = nullptr;
    bool owned = false;
    bool valid = true;
};

// Normalizes newlines and drops control and object characters from typed or
// pasted text; '\n' separates paragraphs, U+2028 stays a soft line break.
QString cleanInsertedText(QString text);

StepText textStep(const RichTextDocument &doc, const Paragraph &p, int offset, int length,
                  const QString &inserted, const QVector<Span> &insertedSpans,
                  const QVector<RichObject> &insertedObjects = {});

} // namespace QindaTK::RichText::Detail
