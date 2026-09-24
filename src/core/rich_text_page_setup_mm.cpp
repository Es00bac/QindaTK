// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_document.h"
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {
constexpr double pointsPerMillimetre = 72.0 / 25.4;

bool finiteNumber(const QVariant &value, double *number)
{
    bool valid = false;
    *number = value.toDouble(&valid);
    return valid && std::isfinite(*number);
}
}

QVariantMap RichTextDocument::pageSetupMm() const
{
    const auto mm = [](double points) { return points / pointsPerMillimetre; };
    return {{QStringLiteral("widthMm"), mm(m_page.widthPt)},
            {QStringLiteral("heightMm"), mm(m_page.heightPt)},
            {QStringLiteral("unit"), QStringLiteral("mm")},
            {QStringLiteral("orientation"), m_page.widthPt > m_page.heightPt
                 ? QStringLiteral("landscape") : QStringLiteral("portrait")},
            {QStringLiteral("scope"), QStringLiteral("document")},
            {QStringLiteral("marginsMm"), QVariantMap{
                 {QStringLiteral("top"), mm(m_page.marginTopPt)},
                 {QStringLiteral("right"), mm(m_page.marginRightPt)},
                 {QStringLiteral("bottom"), mm(m_page.marginBottomPt)},
                 {QStringLiteral("left"), mm(m_page.marginLeftPt)}}},
            {QStringLiteral("gutterMm"), 0.0},
            {QStringLiteral("columns"), 1},
            {QStringLiteral("bleedMm"), 0.0}};
}

bool RichTextDocument::applyPageSetupMm(const QVariantMap &settings)
{
    const QString scope = settings.value(QStringLiteral("scope"), QStringLiteral("document")).toString();
    double gutter = 0, bleed = 0;
    if (scope != QStringLiteral("document") || settings.value(QStringLiteral("columns"), 1).toInt() != 1
        || !finiteNumber(settings.value(QStringLiteral("gutterMm"), 0), &gutter) || gutter != 0
        || !finiteNumber(settings.value(QStringLiteral("bleedMm"), 0), &bleed) || bleed != 0) {
        reject(ErrorCode::Unsupported, QStringLiteral("Sections, columns, gutters and bleed are not yet supported by the rich-text paginator"));
        return false;
    }
    const QVariantMap margins = settings.value(QStringLiteral("marginsMm")).toMap();
    double width = 0, height = 0, top = 0, right = 0, bottom = 0, left = 0;
    if (!finiteNumber(settings.value(QStringLiteral("widthMm")), &width)
        || !finiteNumber(settings.value(QStringLiteral("heightMm")), &height)
        || !finiteNumber(margins.value(QStringLiteral("top")), &top)
        || !finiteNumber(margins.value(QStringLiteral("right")), &right)
        || !finiteNumber(margins.value(QStringLiteral("bottom")), &bottom)
        || !finiteNumber(margins.value(QStringLiteral("left")), &left)) {
        reject(ErrorCode::Validation, QStringLiteral("Page dimensions and margins must be finite numbers"));
        return false;
    }
    const QString orientation = settings.value(QStringLiteral("orientation"), QStringLiteral("portrait")).toString();
    if (orientation != QStringLiteral("portrait") && orientation != QStringLiteral("landscape")) {
        reject(ErrorCode::Validation, QStringLiteral("Unknown page orientation"));
        return false;
    }
    if ((orientation == QStringLiteral("landscape")) != (width > height)) std::swap(width, height);
    RichText::PageSetup page;
    page.widthPt = width * pointsPerMillimetre;
    page.heightPt = height * pointsPerMillimetre;
    page.marginTopPt = top * pointsPerMillimetre;
    page.marginRightPt = right * pointsPerMillimetre;
    page.marginBottomPt = bottom * pointsPerMillimetre;
    page.marginLeftPt = left * pointsPerMillimetre;
    return setPageSetup(page);
}

} // namespace QindaTK
