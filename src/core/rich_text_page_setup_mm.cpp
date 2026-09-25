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
            {QStringLiteral("gutterMm"), mm(m_page.columnGapPt)},
            {QStringLiteral("columns"), m_page.columns},
            {QStringLiteral("bleedMm"), mm(m_page.bleedPt)}};
}

bool RichTextDocument::applyPageSetupMm(const QVariantMap &settings)
{
    // AGENT-NOTE (D-133): page geometry is document-wide; per-section or
    // per-page geometry is refused (docs/printing.md#bleed-and-page-scope).
    // Bleed (WP-053) is document setup that print tickets may include.
    const QString scope = settings.value(QStringLiteral("scope"), QStringLiteral("document")).toString();
    if (scope != QStringLiteral("document")) {
        reject(ErrorCode::Unsupported, QStringLiteral("Section and page scopes are not supported by the rich-text paginator"));
        return false;
    }
    double bleed = m_page.bleedPt / pointsPerMillimetre;
    if (!finiteNumber(settings.value(QStringLiteral("bleedMm"), bleed), &bleed) || bleed < 0
        || bleed * pointsPerMillimetre > 72.0 + 1e-9) {
        reject(ErrorCode::Validation, QStringLiteral("Bleed must be 0 to 25.4 mm"));
        return false;
    }
    double columns = 1, gutter = m_page.columnGapPt / pointsPerMillimetre;
    if (!finiteNumber(settings.value(QStringLiteral("columns"), 1), &columns) || columns != std::floor(columns)
        || columns < 1 || columns > 24 || !finiteNumber(settings.value(QStringLiteral("gutterMm"), gutter), &gutter)
        || gutter < 0) {
        reject(ErrorCode::Validation, QStringLiteral("Choose 1 to 24 columns and a non-negative column gap"));
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
    RichText::PageSetup page = m_page; // keeps the running page regions
    page.widthPt = width * pointsPerMillimetre;
    page.heightPt = height * pointsPerMillimetre;
    page.marginTopPt = top * pointsPerMillimetre;
    page.marginRightPt = right * pointsPerMillimetre;
    page.marginBottomPt = bottom * pointsPerMillimetre;
    page.marginLeftPt = left * pointsPerMillimetre;
    page.columns = int(columns);
    page.columnGapPt = gutter * pointsPerMillimetre;
    page.bleedPt = qMin(72.0, bleed * pointsPerMillimetre);
    return setPageSetup(page);
}

} // namespace QindaTK
