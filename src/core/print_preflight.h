// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_output.h"

namespace QindaTK {
class RichTextDocument;
class RichTextImageRenderer;
class RichTextLayout;
}

// Production checks before output (WP-053, docs/printing.md#preflight).
namespace QindaTK::Print {

// A preflight profile: how strict the checks are for one kind of output.
struct Profile {
    QString id;                 // "print" (office printer) or "press" (commercial print)
    QString label;
    double minimumDpi = 150;    // effective image resolution on the sheet
    bool strict = false;        // missing fonts, review markup and low resolution block output
};
QVector<Profile> profiles();
Profile profile(const QString &id); // unknown IDs give "print"

// One problem, in PreflightPanel's issue shape (toMap).
// AGENT-CONTRACT (D-132, docs/printing.md#preflight): `id` is derived from the
// check and its target ("font-missing:<family>", "image-missing:<objectId>"),
// so it survives a re-run and a repair can be verified by its absence.
// PrintAdapter.repair and PreflightPanel's selection both key on it.
struct Issue {
    QString id;
    QString severity;           // blocker, error, warning, info
    QString category;           // Output, Fonts, Assets, Layout, Fields, Review
    QString title;
    QString description;
    QString targetId;           // font:<family>, object:<id>, frame:<id>, paragraph:<id>,
                                // field:<id>, equation:<id>, review:<id>, setting:<key>, page:<n>
    QString scope;              // document, page, object, output
    QStringList repairActionIds;
    QString repairLabel;
    bool repairAvailable = false;
    QString paragraphId;        // where to navigate in the text (empty: none)
    int offset = 0;
    int pageIndex = -1;         // zero-based layout page, -1 when unknown
    QString fieldPath;          // output issues: the ticket field
    QVariantMap toMap() const;
};

struct PreflightInput {
    RichTextDocument *document = nullptr;
    RichTextLayout *layout = nullptr;        // paged and fully paginated
    RichTextImageRenderer *images = nullptr; // decodes image sources like the output does
    Ticket ticket;
    Capabilities destination;
    Profile profile;
};

// Every issue in a stable order: output, fonts, assets, layout, fields, review.
QVector<Issue> preflight(const PreflightInput &input);
bool blocksOutput(const Issue &issue);      // blocker or error

// The replacement a "replace-font" repair uses for a missing family.
QString replacementFamily(const RichTextDocument &document, const QString &missing);

} // namespace QindaTK::Print
