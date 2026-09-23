// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_types.h"

#include <QHashFunctions>

namespace QindaTK::RichText {

size_t qHash(const CharFormat &f, size_t seed) noexcept
{
    const auto rgba = [](const QColor &c) { return c.isValid() ? quint64(c.rgba64()) + 1 : quint64(0); };
    return qHashMulti(seed, f.family, f.sizePt, f.weight, int(f.italic) * 9 + int(f.underline) * 3
                      + int(f.strike), rgba(f.color), rgba(f.background), f.href,
                      int(f.verticalAlign), f.objectId);
}

bool CharFormatPatch::isEmpty() const
{
    return !family && !sizePt && !weight && !italic && !underline && !strike
        && !color && !background && !href && !verticalAlign;
}

CharFormat CharFormatPatch::applied(CharFormat format) const
{
    if (family) format.family = *family;
    if (sizePt) format.sizePt = *sizePt;
    if (weight) format.weight = *weight;
    if (italic) format.italic = *italic;
    if (underline) format.underline = *underline;
    if (strike) format.strike = *strike;
    if (color) format.color = *color;
    if (background) format.background = *background;
    if (href) format.href = *href;
    if (verticalAlign) format.verticalAlign = *verticalAlign;
    return format;
}

bool ParagraphFormatPatch::isEmpty() const
{
    return !alignment && !direction && !indentStartPt && !indentEndPt
        && !firstLineIndentPt && !spaceBeforePt && !spaceAfterPt && !lineHeight
        && !lineHeightPt && !headingLevel && !listId && !listLevel && !keepWithNext
        && !keepTogether && !pageBreakBefore && !styleId;
}

ParagraphFormat ParagraphFormatPatch::applied(ParagraphFormat format) const
{
    if (alignment) format.alignment = *alignment;
    if (direction) format.direction = *direction;
    if (indentStartPt) format.indentStartPt = *indentStartPt;
    if (indentEndPt) format.indentEndPt = *indentEndPt;
    if (firstLineIndentPt) format.firstLineIndentPt = *firstLineIndentPt;
    if (spaceBeforePt) format.spaceBeforePt = *spaceBeforePt;
    if (spaceAfterPt) format.spaceAfterPt = *spaceAfterPt;
    if (lineHeight) format.lineHeight = *lineHeight;
    if (lineHeightPt) format.lineHeightPt = *lineHeightPt;
    if (headingLevel) format.headingLevel = *headingLevel;
    if (listId) format.listId = *listId;
    if (listLevel) format.listLevel = *listLevel;
    if (keepWithNext) format.keepWithNext = *keepWithNext;
    if (keepTogether) format.keepTogether = *keepTogether;
    if (pageBreakBefore) format.pageBreakBefore = *pageBreakBefore;
    if (styleId) format.styleId = *styleId;
    return format;
}

bool isListKindOrdered(ListKind kind)
{
    return kind != ListKind::Disc && kind != ListKind::Circle && kind != ListKind::Square;
}

static QString alphaNumber(int number, bool upper)
{
    // Spreadsheet-style bijective base 26: 1 -> a, 26 -> z, 27 -> aa.
    QString text;
    for (int n = qMax(1, number); n > 0; n = (n - 1) / 26)
        text.prepend(QChar((upper ? u'A' : u'a') + (n - 1) % 26));
    return text;
}

static QString romanNumber(int number, bool upper)
{
    if (number <= 0 || number >= 4000) return QString::number(number);
    static const struct { int value; const char *text; } table[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
        {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};
    QString text;
    for (const auto &entry : table)
        while (number >= entry.value) {
            text += QLatin1String(entry.text);
            number -= entry.value;
        }
    return upper ? text.toUpper() : text;
}

QString listMarkerText(ListKind kind, int number)
{
    switch (kind) {
    case ListKind::Disc: return QStringLiteral("•");
    case ListKind::Circle: return QStringLiteral("◦");
    case ListKind::Square: return QStringLiteral("▪");
    case ListKind::Decimal: return QString::number(number) + u'.';
    case ListKind::LowerAlpha: return alphaNumber(number, false) + u'.';
    case ListKind::UpperAlpha: return alphaNumber(number, true) + u'.';
    case ListKind::LowerRoman: return romanNumber(number, false) + u'.';
    case ListKind::UpperRoman: return romanNumber(number, true) + u'.';
    }
    return {};
}

} // namespace QindaTK::RichText
