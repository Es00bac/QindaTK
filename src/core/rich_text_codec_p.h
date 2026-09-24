// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_types.h"
#include <QJsonObject>
#include <QStringList>

// Shared name tables and JSON helpers of the qindatk.richtext codec.
namespace QindaTK::RichText::Codec {

inline const QStringList &alignmentNames()
{
    static const QStringList names{QStringLiteral("start"), QStringLiteral("end"),
                                   QStringLiteral("left"), QStringLiteral("right"),
                                   QStringLiteral("center"), QStringLiteral("justify")};
    return names;
}
inline const QStringList &directionNames()
{
    static const QStringList names{QStringLiteral("auto"), QStringLiteral("ltr"), QStringLiteral("rtl")};
    return names;
}
inline const QStringList &verticalAlignNames()
{
    static const QStringList names{QStringLiteral("normal"), QStringLiteral("super"), QStringLiteral("sub")};
    return names;
}
inline const QStringList &placementNames()
{
    static const QStringList names{QStringLiteral("inline"), QStringLiteral("floatStart"),
                                   QStringLiteral("floatEnd"), QStringLiteral("page")};
    return names;
}
inline const QStringList &listKindNames()
{
    static const QStringList names{QStringLiteral("disc"), QStringLiteral("circle"),
                                   QStringLiteral("square"), QStringLiteral("decimal"),
                                   QStringLiteral("lowerAlpha"), QStringLiteral("upperAlpha"),
                                   QStringLiteral("lowerRoman"), QStringLiteral("upperRoman")};
    return names;
}

inline QString colorName(const QColor &color)
{
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QJsonObject charFormatJson(const CharFormat &format);
QJsonObject paragraphFormatJson(const ParagraphFormat &format);
QJsonObject objectJson(const RichObject &object);
QJsonObject listJson(const ListDefinition &list);

} // namespace QindaTK::RichText::Codec
