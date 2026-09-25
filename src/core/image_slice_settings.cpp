// SPDX-License-Identifier: LGPL-3.0-or-later
// ImageSliceItem links (cursor, transfer, overlay compatibility) and saved
// view settings (D-260). Painting lives in image_slice_item.cpp, oblique
// geometry in image_slice_reslice.cpp.
#include "image_slice_item.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
const char *const PlaneNames[] = {"axial", "coronal", "sagittal", "oblique"};
// A transfer function applies when either side is unitless or both agree.
QString unitMismatch(const TransferFunctionModel *transfer, const VoxelVolume *volume, const QString &what)
{
    if (!transfer || !volume || transfer->unit().isEmpty() || volume->valueUnit().isEmpty()
        || transfer->unit() == volume->valueUnit()) return {};
    return QStringLiteral("%1 is in %2 but the volume stores %3")
        .arg(what, transfer->unit(), volume->valueUnit());
}
} // namespace

bool ImageSliceItem::cursorLinked() const { return m_cursorLinked; }

void ImageSliceItem::refreshLinks()
{
    QStringList reasons;
    // AGENT-GUARD: the views index the cursor's point in their own voxels, so
    // a cursor bound to another grid would mark a different calibrated point.
    const VoxelVolume *cursorVolume = m_cursor ? m_cursor->volume() : nullptr;
    const QString cursorReason = m_volume && cursorVolume && cursorVolume != m_volume
        ? m_volume->geometryMismatch(cursorVolume) : QString();
    if (!cursorReason.isEmpty()) reasons.append(QStringLiteral("Cursor not linked: %1").arg(cursorReason));
    const QString transferReason = unitMismatch(m_transfer, m_volume, QStringLiteral("The transfer function"));
    if (!transferReason.isEmpty()) reasons.append(transferReason);
    QString overlayReason;
    if (m_overlay) {
        if (!m_volume) overlayReason = QStringLiteral("No base volume");
        else if (!m_overlayTransfer) overlayReason = QStringLiteral("The overlay needs a transfer function");
        else overlayReason = m_volume->geometryMismatch(m_overlay);
        if (overlayReason.isEmpty())
            overlayReason = unitMismatch(m_overlayTransfer, m_overlay, QStringLiteral("The overlay transfer function"));
    }
    const bool cursorLinked = cursorReason.isEmpty(), transferUsable = transferReason.isEmpty();
    const bool overlayUsable = m_overlay && overlayReason.isEmpty();
    const QString linkError = reasons.join(QStringLiteral("; "));
    const bool redraw = transferUsable != m_transferUsable || overlayUsable != m_overlayUsable
        || cursorLinked != m_cursorLinked;
    if (linkError == m_linkError && overlayReason == m_overlayError && !redraw) return;
    m_cursorLinked = cursorLinked;
    m_transferUsable = transferUsable;
    m_overlayUsable = overlayUsable;
    m_linkError = linkError;
    m_overlayError = overlayReason;
    if (redraw) { m_dirty = true; update(); }
    emit linkChanged();
    if (redraw) emit sliceChanged();
}

void ImageSliceItem::setOverlay(VoxelVolume *overlay)
{
    if (m_overlay == overlay) return;
    if (m_overlay) disconnect(m_overlay, nullptr, this, nullptr);
    m_overlay = overlay;
    if (overlay) connect(overlay, &VoxelVolume::changed, this, [this] { refreshLinks(); invalidate(); });
    refreshLinks(); invalidate(); emit changed();
}
void ImageSliceItem::setOverlayTransfer(TransferFunctionModel *transfer)
{
    if (m_overlayTransfer == transfer) return;
    if (m_overlayTransfer) disconnect(m_overlayTransfer, nullptr, this, nullptr);
    m_overlayTransfer = transfer;
    if (transfer) connect(transfer, &TransferFunctionModel::changed, this, [this] { refreshLinks(); invalidate(); });
    refreshLinks(); invalidate(); emit changed();
}
void ImageSliceItem::setOverlayOpacity(double opacity)
{
    if (!std::isfinite(opacity)) return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (m_overlayOpacity == opacity) return;
    m_overlayOpacity = opacity; invalidate(); emit changed();
}

QRgb ImageSliceItem::shadeAt(double x, double y, double z) const
{
    // Outside the volume is not "missing data": it stays transparent.
    const double point[3] = {x, y, z};
    const int shape[3] = {m_volume->sizeX(), m_volume->sizeY(), m_volume->sizeZ()};
    for (int axis = 0; axis < 3; ++axis)
        if (!(point[axis] >= -0.5 && point[axis] <= shape[axis] - 0.5)) return qRgba(0, 0, 0, 0);
    const QRgb base = colorFor(m_volume->sampleValue(x, y, z, m_channel, m_frame, m_interpolation));
    if (!m_overlayUsable || !m_overlay || !m_overlayTransfer || m_overlayOpacity <= 0) return base;
    // Straight-alpha "over": the overlay's channel 0 (at the view's frame when
    // it has one) through its transfer colour and opacity, scaled by
    // overlayOpacity, over the base colour.
    const QColor top = m_overlayTransfer->colorAt(
        m_overlay->sampleValue(x, y, z, 0, m_frame < m_overlay->frames() ? m_frame : 0, m_interpolation));
    const double a = top.alphaF() * m_overlayOpacity, b = qAlpha(base) / 255.0;
    const double out = a + b * (1 - a);
    if (out <= 0) return qRgba(0, 0, 0, 0);
    const auto mix = [&](int over, int under) {
        return int(std::lround((over * a + under * b * (1 - a)) / out));
    };
    return qRgba(mix(top.red(), qRed(base)), mix(top.green(), qGreen(base)),
                 mix(top.blue(), qBlue(base)), int(std::lround(out * 255)));
}

QVariantMap ImageSliceItem::saveSettings() const
{
    QVariantMap settings{
        {QStringLiteral("plane"), QString::fromLatin1(PlaneNames[m_plane])},
        {QStringLiteral("resliceOrientation"), resliceOrientation()},
        {QStringLiteral("windowCenter"), m_windowCenter},
        {QStringLiteral("windowWidth"), m_windowWidth},
        {QStringLiteral("interpolation"), m_interpolation == VoxelVolume::Linear
                                              ? QStringLiteral("linear") : QStringLiteral("nearest")},
        {QStringLiteral("channel"), m_channel},
        {QStringLiteral("frame"), m_frame},
        {QStringLiteral("overlayOpacity"), m_overlayOpacity}};
    if (m_volume) settings.insert(QStringLiteral("volume"), m_volume->identity());
    return settings;
}

bool ImageSliceItem::restoreSettings(const QVariantMap &settings)
{
    // AGENT-GUARD: validate everything first, then apply; a rejected map
    // leaves every view setting untouched.
    const auto reject = [this](const QString &message) {
        m_error = message; emit changed(); return false;
    };
    const auto number = [&settings](const char *key, double *out) {
        bool ok = false;
        *out = settings.value(QString::fromLatin1(key)).toDouble(&ok);
        return ok && std::isfinite(*out);
    };
    if (m_volume) {
        if (!settings.contains(QStringLiteral("volume")))
            return reject(QStringLiteral("The settings do not name their volume"));
        const QString mismatch = m_volume->identityMismatch(settings.value(QStringLiteral("volume")).toMap());
        if (!mismatch.isEmpty()) return reject(mismatch);
    }
    const QString planeName = settings.value(QStringLiteral("plane")).toString();
    const auto plane = std::find_if(std::begin(PlaneNames), std::end(PlaneNames),
                                    [&](const char *name) { return planeName == QLatin1String(name); });
    const QString interpolation = settings.value(QStringLiteral("interpolation")).toString();
    double center, width, channel, frame, opacity;
    if (plane == std::end(PlaneNames) || !number("windowCenter", &center) || !number("windowWidth", &width)
        || width <= 0 || !number("channel", &channel) || !number("frame", &frame)
        || channel < 0 || frame < 0 || channel != std::floor(channel) || frame != std::floor(frame)
        || !number("overlayOpacity", &opacity) || opacity < 0 || opacity > 1
        || (interpolation != QLatin1String("linear") && interpolation != QLatin1String("nearest")))
        return reject(QStringLiteral("Invalid image slice settings"));
    if (m_volume && (channel >= m_volume->channels() || frame >= m_volume->frames()))
        return reject(QStringLiteral("The saved channel or frame is outside the volume"));
    double reslice[9];
    if (!parseOrientation(settings.value(QStringLiteral("resliceOrientation")).toList(), reslice))
        return reject(QStringLiteral("Reslice orientation must be an orthonormal 3×3 matrix"));
    std::copy(reslice, reslice + 9, m_reslice);
    m_plane = Plane(plane - std::begin(PlaneNames));
    m_windowCenter = center;
    m_windowWidth = width;
    m_channel = int(channel);
    m_frame = int(frame);
    m_interpolation = interpolation == QLatin1String("linear") ? VoxelVolume::Linear : VoxelVolume::Nearest;
    m_overlayOpacity = opacity;
    m_error.clear();
    invalidate();
    emit changed();
    emit sliceChanged();
    return true;
}
} // namespace QindaTK
