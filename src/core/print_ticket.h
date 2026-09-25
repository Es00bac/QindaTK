// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <QMarginsF>
#include <QSizeF>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <optional>

// Print tickets and destination capabilities (WP-053, docs/printing.md).
namespace QindaTK::Print {

// One-based inclusive range of the document's laid-out pages; `last` -1 runs
// to the last page ("5-").
struct PageRange {
    int first = 1;
    int last = 1;
    friend bool operator==(const PageRange &, const PageRange &) = default;
};

// A sheet size in portrait orientation. The "document" paper has no size: the
// sheet follows the document page (plus bleed and crop marks).
struct Paper {
    QString id;
    QString label;
    QSizeF sizePt;
};

// AGENT-CONTRACT (docs/printing.md#tickets): ticketFromMap/ticketToMap use
// the keys of PrintPreview.settings (paperId, orientation, scaleMode,
// scalePercent, duplex, copies, printerId, colorMode) plus pages, pageRange,
// outputPath, collate, includeBleed and cropMarks. PrintPreview, PrintAdapter
// and the example read the same map; renaming a key breaks all three.
struct Ticket {
    QString printerId = QStringLiteral("pdf");
    QString outputPath;                   // file destinations: local path or file: URL
    QVector<PageRange> ranges;            // empty: every page
    QString paperId = QStringLiteral("document");
    QString orientation = QStringLiteral("auto");   // auto, portrait, landscape
    QString scaleMode = QStringLiteral("fit");      // fit, actual, percent
    double scalePercent = 100;            // 1..1000, with scaleMode "percent"
    int copies = 1;
    bool collate = true;
    QString duplex = QStringLiteral("simplex");     // simplex, long-edge, short-edge
    QString colorMode = QStringLiteral("color");    // color, grayscale
    bool includeBleed = false;            // extend the sheet by the document's bleed
    bool cropMarks = false;               // trim marks outside the bleed
};

// What one destination (a printer or a file writer) accepts. Options outside
// these lists are refused by validateTicket and named in `unavailable`.
struct Capabilities {
    QString printerId;
    QString label;
    QString kind = QStringLiteral("printer");       // printer, file
    bool available = true;
    QString status;                       // device state, or why it is unavailable
    QVector<Paper> papers;
    QStringList orientations{QStringLiteral("auto"), QStringLiteral("portrait"), QStringLiteral("landscape")};
    QStringList scaleModes{QStringLiteral("fit"), QStringLiteral("actual"), QStringLiteral("percent")};
    QStringList duplexModes{QStringLiteral("simplex")};
    QStringList colorModes{QStringLiteral("color")};
    int maxCopies = 1;
    bool collate = false;
    bool bleed = true;                    // sheets may carry bleed and crop marks
    QMarginsF printableMarginsPt;         // the device cannot mark this edge of the sheet
    QStringList unavailable;              // user-facing names of options it lacks
};

// The document paper and A3, A4, A5, Letter, Legal and Tabloid (exact sizes).
QVector<Paper> standardPapers();
// The destination's paper `id`, else the standard one, else the document paper.
Paper resolvePaper(const Capabilities &destination, const QString &id);

// Syntax only ("1-3, 5, 8-"): bounds are checked against the page count by
// validateTicket, so a selection can outlive an edit that shortens the document.
std::optional<QVector<PageRange>> parsePageRanges(const QString &text, OperationError *error = nullptr);
QString formatPageRanges(const QVector<PageRange> &ranges);
// Zero-based source pages in output order; pages beyond `pageCount` are left out.
QVector<int> selectedPages(const QVector<PageRange> &ranges, int pageCount);

// Applies `map` over `base`; malformed values fail with the key as fieldPath.
std::optional<Ticket> ticketFromMap(const QVariantMap &map, const Ticket &base, OperationError *error = nullptr);
QVariantMap ticketToMap(const Ticket &ticket, int pageCount);
// Every option the destination refuses or the document cannot satisfy.
QVector<ValidationIssue> validateTicket(const Ticket &ticket, const Capabilities &destination, int pageCount);
// PrintPreview.capabilities shape: paperOptions, orientationOptions, ... (id, label).
QVariantMap capabilitiesToMap(const Capabilities &destination);
QString optionLabel(const QString &key, const QString &value);
// The first value the destination supports for a ticket field (repairs).
QVariant supportedValue(const Capabilities &destination, const QString &fieldPath);

} // namespace QindaTK::Print
