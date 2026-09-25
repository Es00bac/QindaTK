// SPDX-License-Identifier: LGPL-3.0-or-later
#include "volume_view_item.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr double DefaultYaw = 30, DefaultPitch = 20;

double wrapDegrees(double degrees)
{
    double wrapped = std::fmod(degrees + 180.0, 360.0);
    if (wrapped < 0) wrapped += 360.0;
    return wrapped - 180.0;
}
QVariantList vectorList(const QVector3D &v) { return {double(v.x()), double(v.y()), double(v.z())}; }
bool clipPair(const QVector3D &low, const QVector3D &high)
{
    for (int axis = 0; axis < 3; ++axis) {
        const double a = low[axis], b = high[axis];
        if (!std::isfinite(a) || !std::isfinite(b) || a < 0 || b > 1 || a >= b) return false;
    }
    return true;
}
bool readVector(const QVariant &value, QVector3D *out)
{
    const QVariantList list = value.toList();
    if (list.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        const double v = list[i].toDouble(&ok);
        if (!ok || !std::isfinite(v)) return false;
        (*out)[i] = float(v);
    }
    return true;
}
} // namespace

VolumeViewItem::VolumeViewItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_params.yawDegrees = DefaultYaw;
    m_params.pitchDegrees = DefaultPitch;
    m_worker = new VolumeRenderWorker(this);
    m_worker->delivered = [this](const VolumeRenderWorker::Result &result) { delivered(result); };
}
VolumeViewItem::~VolumeViewItem() { waitForWorker(); }
void VolumeViewItem::waitForWorker() { if (m_worker) m_worker->cancelAndWait(); }

void VolumeViewItem::fail(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}
void VolumeViewItem::invalidate()
{
    // A new generation makes every image in flight obsolete (D-261).
    ++m_generation;
    m_renderCanceled = false;
    update();
}
void VolumeViewItem::invalidateVolume()
{
    ++m_volumeGeneration;
    m_textureDirty = true;
    m_packFailed = false;
    m_packReady = false;
    m_packedBytes.clear();
    invalidate();
}
void VolumeViewItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) invalidate();
}
void VolumeViewItem::releaseResources()
{
    // The window is going away or changing: the next node is built fresh.
    m_contentIsGpu = false;
    m_textureDirty = true;
    m_requestedGeneration = m_requestedVolumeGeneration = 0;
}

void VolumeViewItem::setVolume(VoxelVolume *volume)
{
    if (m_volume == volume) return;
    // AGENT-GUARD: the worker may be reading the old volume; it must stop
    // before this item forgets it (the host may delete it next).
    waitForWorker();
    if (m_volume) disconnect(m_volume, nullptr, this, nullptr);
    m_volume = volume;
    if (volume) {
        connect(volume, &VoxelVolume::changed, this, [this] { refreshLinks(); invalidateVolume(); });
        connect(volume, &VoxelVolume::aboutToBeDestroyed, this, &VolumeViewItem::waitForWorker,
                Qt::DirectConnection);
    }
    refreshLinks(); invalidateVolume(); emit sourceChanged();
}
void VolumeViewItem::setCursor(VolumeCursor *cursor)
{
    if (m_cursor == cursor) return;
    if (m_cursor) disconnect(m_cursor, nullptr, this, nullptr);
    m_cursor = cursor;
    if (cursor) connect(cursor, &VolumeCursor::changed, this, &VolumeViewItem::refreshLinks);
    refreshLinks(); emit sourceChanged();
}
void VolumeViewItem::setTransfer(TransferFunctionModel *transfer)
{
    if (m_transfer == transfer) return;
    if (m_transfer) disconnect(m_transfer, nullptr, this, nullptr);
    m_transfer = transfer;
    // Previews during a transfer gesture repaint too (changed, not committed).
    if (transfer) connect(transfer, &TransferFunctionModel::changed, this, [this] { refreshLinks(); invalidate(); });
    refreshLinks(); invalidate(); emit sourceChanged();
}
void VolumeViewItem::setChannel(int channel)
{
    if (m_params.channel == channel || channel < 0) return;
    m_params.channel = channel; invalidateVolume(); emit settingsChanged();
}
void VolumeViewItem::setFrame(int frame)
{
    if (m_params.frame == frame || frame < 0) return;
    m_params.frame = frame; invalidateVolume(); emit settingsChanged();
}
void VolumeViewItem::setInterpolation(VoxelVolume::Interpolation interpolation)
{
    if (m_params.interpolation == interpolation) return;
    m_params.interpolation = interpolation; invalidate(); emit settingsChanged();
}
void VolumeViewItem::setMode(Mode mode)
{
    const auto next = VolumeRenderMode(int(mode));
    if (m_params.mode == next) return;
    m_params.mode = next; invalidate(); emit settingsChanged();
}
void VolumeViewItem::setYaw(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double next = wrapDegrees(degrees);
    if (m_params.yawDegrees == next) return;
    m_params.yawDegrees = next; invalidate(); emit settingsChanged();
}
void VolumeViewItem::setPitch(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double next = std::clamp(degrees, -90.0, 90.0);
    if (m_params.pitchDegrees == next) return;
    m_params.pitchDegrees = next; invalidate(); emit settingsChanged();
}
void VolumeViewItem::setZoom(double zoom)
{
    if (!std::isfinite(zoom)) return;
    const double next = std::clamp(zoom, 0.1, 20.0);
    if (m_params.zoom == next) return;
    m_params.zoom = next; invalidate(); emit settingsChanged();
}
void VolumeViewItem::setSamplingRate(double rate)
{
    if (!std::isfinite(rate)) return;
    const double next = std::clamp(rate, 0.25, 8.0);
    if (m_params.samplingRate == next) return;
    m_params.samplingRate = next; invalidate(); emit settingsChanged();
}
QVector3D VolumeViewItem::clipLow() const
{
    return QVector3D(float(m_params.clipLow[0]), float(m_params.clipLow[1]), float(m_params.clipLow[2]));
}
QVector3D VolumeViewItem::clipHigh() const
{
    return QVector3D(float(m_params.clipHigh[0]), float(m_params.clipHigh[1]), float(m_params.clipHigh[2]));
}
void VolumeViewItem::setClipLow(const QVector3D &low)
{
    if (low == clipLow()) return;
    if (!clipPair(low, clipHigh())) { fail(QStringLiteral("Clip bounds must satisfy 0 ≤ low < high ≤ 1")); return; }
    for (int axis = 0; axis < 3; ++axis) m_params.clipLow[axis] = low[axis];
    fail({}); invalidate(); emit settingsChanged();
}
void VolumeViewItem::setClipHigh(const QVector3D &high)
{
    if (high == clipHigh()) return;
    if (!clipPair(clipLow(), high)) { fail(QStringLiteral("Clip bounds must satisfy 0 ≤ low < high ≤ 1")); return; }
    for (int axis = 0; axis < 3; ++axis) m_params.clipHigh[axis] = high[axis];
    fail({}); invalidate(); emit settingsChanged();
}
void VolumeViewItem::setWindowCenter(double value)
{
    if (!std::isfinite(value) || m_windowCenter == value) return;
    m_windowCenter = value; invalidate(); emit settingsChanged();
}
void VolumeViewItem::setWindowWidth(double value)
{
    if (!std::isfinite(value) || value <= 0 || m_windowWidth == value) return;
    m_windowWidth = value; invalidate(); emit settingsChanged();
}
void VolumeViewItem::setBackgroundColor(const QColor &color)
{
    if (!color.isValid() || m_background == color) return;
    m_background = color; update(); emit appearanceChanged();
}
void VolumeViewItem::setRenderer(Renderer renderer)
{
    if (m_renderer == renderer) return;
    m_renderer = renderer; invalidateVolume(); emit appearanceChanged();
}
void VolumeViewItem::setMaximumTextureBytes(qint64 bytes)
{
    if (bytes <= 0 || m_maximumTextureBytes == bytes) return;
    m_maximumTextureBytes = bytes; invalidateVolume(); emit appearanceChanged();
}

void VolumeViewItem::orbit(double deltaYaw, double deltaPitch)
{
    setYaw(m_params.yawDegrees + deltaYaw);
    setPitch(m_params.pitchDegrees + deltaPitch);
}
void VolumeViewItem::resetView()
{
    setYaw(DefaultYaw);
    setPitch(DefaultPitch);
    setZoom(1);
}
VolumeTransferTable VolumeViewItem::transferTable() const
{
    // A transfer function in another value unit is ignored (linkError).
    return m_transfer && m_transferUsable ? VolumeTransferTable::fromModel(*m_transfer)
                                          : VolumeTransferTable::fromWindow(m_windowCenter, m_windowWidth);
}
QVariantList VolumeViewItem::pick(double x, double y) const
{
    double voxel[3];
    if (!m_volume || !VolumeRaycaster::pick(*m_volume, transferTable(), m_params, size(), QPointF(x, y), voxel))
        return {};
    return {voxel[0], voxel[1], voxel[2]};
}
bool VolumeViewItem::setCursorFromPick(double x, double y)
{
    const QVariantList voxel = pick(x, y);
    if (voxel.size() != 3 || !m_cursor || !m_cursorLinked) return false;
    if (!m_cursor->volume()) m_cursor->setVolume(m_volume);
    if (!m_cursor->setVoxelPoint(voxel[0].toDouble(), voxel[1].toDouble(), voxel[2].toDouble())) return false;
    emit picked({m_cursor->x(), m_cursor->y(), m_cursor->z()});
    return true;
}
QImage VolumeViewItem::renderReference(int width, int height) const
{
    if (!m_volume || width < 1 || height < 1) return {};
    return VolumeRaycaster::render(*m_volume, transferTable(), m_params, QSize(width, height));
}

QVariantMap VolumeViewItem::saveSettings() const
{
    return {{QStringLiteral("yaw"), m_params.yawDegrees},
            {QStringLiteral("pitch"), m_params.pitchDegrees},
            {QStringLiteral("zoom"), m_params.zoom},
            {QStringLiteral("mode"), m_params.mode == VolumeRenderMode::MaximumIntensity
                                         ? QStringLiteral("maximum") : QStringLiteral("composite")},
            {QStringLiteral("interpolation"), m_params.interpolation == VoxelVolume::Linear
                                                  ? QStringLiteral("linear") : QStringLiteral("nearest")},
            {QStringLiteral("samplingRate"), m_params.samplingRate},
            {QStringLiteral("clipLow"), vectorList(clipLow())},
            {QStringLiteral("clipHigh"), vectorList(clipHigh())},
            {QStringLiteral("channel"), m_params.channel},
            {QStringLiteral("frame"), m_params.frame},
            {QStringLiteral("windowCenter"), m_windowCenter},
            {QStringLiteral("windowWidth"), m_windowWidth},
            {QStringLiteral("volume"), m_volume ? m_volume->identity() : QVariantMap()}};
}
bool VolumeViewItem::restoreSettings(const QVariantMap &settings)
{
    // AGENT-GUARD: validate everything first, then apply; a rejected map
    // leaves every view setting (and the voxel source) untouched.
    const auto number = [&settings](const char *key, double *out) {
        bool ok = false;
        *out = settings.value(QString::fromLatin1(key)).toDouble(&ok);
        return ok && std::isfinite(*out);
    };
    double yaw, pitch, zoom, rate, center, width, channel, frame;
    QVector3D low, high;
    const QString mode = settings.value(QStringLiteral("mode")).toString();
    const QString interpolation = settings.value(QStringLiteral("interpolation")).toString();
    if (!number("yaw", &yaw) || !number("pitch", &pitch) || pitch < -90 || pitch > 90
        || !number("zoom", &zoom) || zoom < 0.1 || zoom > 20
        || !number("samplingRate", &rate) || rate < 0.25 || rate > 8
        || !number("windowCenter", &center) || !number("windowWidth", &width) || width <= 0
        || !number("channel", &channel) || !number("frame", &frame) || channel < 0 || frame < 0
        || channel != std::floor(channel) || frame != std::floor(frame)
        || (mode != QLatin1String("composite") && mode != QLatin1String("maximum"))
        || (interpolation != QLatin1String("linear") && interpolation != QLatin1String("nearest"))
        || !readVector(settings.value(QStringLiteral("clipLow")), &low)
        || !readVector(settings.value(QStringLiteral("clipHigh")), &high) || !clipPair(low, high)) {
        fail(QStringLiteral("Invalid volume view settings"));
        return false;
    }
    // D-260: settings name their dataset; another dataset's (or a missing
    // channel/frame) are rejected instead of silently applied.
    if (m_volume) {
        const QString mismatch = settings.contains(QStringLiteral("volume"))
            ? m_volume->identityMismatch(settings.value(QStringLiteral("volume")).toMap())
            : QStringLiteral("The settings do not name their volume");
        if (!mismatch.isEmpty()) { fail(mismatch); return false; }
        if (channel >= m_volume->channels() || frame >= m_volume->frames()) {
            fail(QStringLiteral("The saved channel or frame is outside the volume"));
            return false;
        }
    }
    m_params.yawDegrees = wrapDegrees(yaw);
    m_params.pitchDegrees = pitch;
    m_params.zoom = zoom;
    m_params.samplingRate = rate;
    m_params.mode = mode == QLatin1String("maximum") ? VolumeRenderMode::MaximumIntensity
                                                     : VolumeRenderMode::Composite;
    m_params.interpolation = interpolation == QLatin1String("linear") ? VoxelVolume::Linear
                                                                       : VoxelVolume::Nearest;
    for (int axis = 0; axis < 3; ++axis) {
        m_params.clipLow[axis] = low[axis];
        m_params.clipHigh[axis] = high[axis];
    }
    m_params.channel = int(channel);
    m_params.frame = int(frame);
    m_windowCenter = center;
    m_windowWidth = width;
    fail({});
    invalidateVolume();
    emit settingsChanged();
    return true;
}

} // namespace QindaTK
