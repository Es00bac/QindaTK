// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <QFont>
#include <QTextCharFormat>

class QPaintDevice;

// Private shaping helpers shared by the rich_text_* layout files.
namespace QindaTK::RichText::Shaping {

struct HeadingStyle {
    double sizePt;
    int weight;
};

// Built-in heading defaults until named styles (WP-051) resolve them.
HeadingStyle headingStyle(int level);

// The 72-dpi device every layout font is resolved against (points == px).
const QPaintDevice *layoutDevice();

// Explicit direction, else the first strong character (UAX #9 P2/P3).
bool paragraphIsRightToLeft(const Paragraph &p);

QFont resolvedFont(const RichTextDocument &doc, const CharFormat &format, const ParagraphFormat &paragraph);
QTextCharFormat resolvedFormat(const RichTextDocument &doc, const CharFormat &format,
                               const ParagraphFormat &paragraph);
qreal objectCharAdvance(const QTextCharFormat &format);

} // namespace QindaTK::RichText::Shaping
