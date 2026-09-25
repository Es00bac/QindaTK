// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "transfer_function_model.h"
#include "volume_cursor.h"
#include "volume_raycast.h"
#include "volume_render_worker.h"
#include "voxel_volume.h"
#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Direct volume rendering of a VoxelVolume: orthographic orbit camera,
// composite or maximum-intensity projection, clip box, transfer function
// (or window ramp) and picking into the shared VolumeCursor.
// AGENT-CONTRACT (docs/volume-imaging.md#volume-rendering, D-146/D-147): the
// GPU path (VolumeRenderNode) and the reference path (VolumeRaycaster) draw
// the same algorithm; updatePaintNode picks one per frame and publishes the
// choice as renderPath/fallbackReason. Camera, mode, sampling and clip are
// view settings (saveSettings/restoreSettings); they never touch voxels.
// D-261: volume reads (the reference image, the texture pack) run on a
// VolumeRenderWorker, never in updatePaintNode; a newer request cancels and
// rejects the older one, cancelRendering() stops the current one, and the
// item shows the last completed frame until a current one arrives.
class VolumeViewItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::VoxelVolume *volume READ volume WRITE setVolume NOTIFY sourceChanged)
    Q_PROPERTY(QindaTK::VolumeCursor *cursor READ cursor WRITE setCursor NOTIFY sourceChanged)
    Q_PROPERTY(QindaTK::TransferFunctionModel *transfer READ transfer WRITE setTransfer NOTIFY sourceChanged)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY settingsChanged)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY settingsChanged)
    Q_PROPERTY(QindaTK::VoxelVolume::Interpolation interpolation READ interpolation WRITE setInterpolation NOTIFY settingsChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY settingsChanged)
    Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY settingsChanged)
    Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY settingsChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY settingsChanged)
    Q_PROPERTY(double samplingRate READ samplingRate WRITE setSamplingRate NOTIFY settingsChanged)
    Q_PROPERTY(QVector3D clipLow READ clipLow WRITE setClipLow NOTIFY settingsChanged)
    Q_PROPERTY(QVector3D clipHigh READ clipHigh WRITE setClipHigh NOTIFY settingsChanged)
    Q_PROPERTY(double windowCenter READ windowCenter WRITE setWindowCenter NOTIFY settingsChanged)
    Q_PROPERTY(double windowWidth READ windowWidth WRITE setWindowWidth NOTIFY settingsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY appearanceChanged)
    Q_PROPERTY(Renderer renderer READ renderer WRITE setRenderer NOTIFY appearanceChanged)
    Q_PROPERTY(qint64 maximumTextureBytes READ maximumTextureBytes WRITE setMaximumTextureBytes NOTIFY appearanceChanged)
    Q_PROPERTY(QString renderPath READ renderPath NOTIFY renderPathChanged)
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY renderPathChanged)
    Q_PROPERTY(QSize referenceSize READ referenceSize NOTIFY renderPathChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString linkError READ linkError NOTIFY linkChanged)
public:
    enum Mode { Composite = 0, MaximumIntensity = 1 };
    Q_ENUM(Mode)
    enum Renderer { Automatic = 0, Reference = 1 };
    Q_ENUM(Renderer)
    // AGENT-GUARD: the reference path renders at most this many rays per
    // frame (scaled up with smoothing); lifting it makes one CPU repaint cost
    // up to MaximumSteps samples for every pixel of an arbitrarily large item.
    static constexpr int MaximumReferencePixels = 192 * 192;
    explicit VolumeViewItem(QQuickItem *parent = nullptr);
    ~VolumeViewItem() override;
    VoxelVolume *volume() const { return m_volume; }
    void setVolume(VoxelVolume *volume);
    VolumeCursor *cursor() const { return m_cursor; }
    void setCursor(VolumeCursor *cursor);
    TransferFunctionModel *transfer() const { return m_transfer; }
    void setTransfer(TransferFunctionModel *transfer);
    int channel() const { return m_params.channel; }
    void setChannel(int channel);
    int frame() const { return m_params.frame; }
    void setFrame(int frame);
    VoxelVolume::Interpolation interpolation() const { return m_params.interpolation; }
    void setInterpolation(VoxelVolume::Interpolation interpolation);
    Mode mode() const { return Mode(int(m_params.mode)); }
    void setMode(Mode mode);
    double yaw() const { return m_params.yawDegrees; }
    void setYaw(double degrees);
    double pitch() const { return m_params.pitchDegrees; }
    void setPitch(double degrees);
    double zoom() const { return m_params.zoom; }
    void setZoom(double zoom);
    double samplingRate() const { return m_params.samplingRate; }
    void setSamplingRate(double rate);
    QVector3D clipLow() const;
    void setClipLow(const QVector3D &low);
    QVector3D clipHigh() const;
    void setClipHigh(const QVector3D &high);
    double windowCenter() const { return m_windowCenter; }
    void setWindowCenter(double value);
    double windowWidth() const { return m_windowWidth; }
    void setWindowWidth(double value);
    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &color);
    Renderer renderer() const { return m_renderer; }
    void setRenderer(Renderer renderer);
    qint64 maximumTextureBytes() const { return m_maximumTextureBytes; }
    void setMaximumTextureBytes(qint64 bytes);
    QString renderPath() const { return m_renderPath; }
    QString fallbackReason() const { return m_fallbackReason; }
    QSize referenceSize() const { return m_referenceSize; }
    QString lastError() const { return m_error; }
    // Background volume work is in flight (an image or a texture pack).
    bool busy() const { return m_busy; }
    // Why the transfer function or cursor is ignored (D-260), else empty.
    QString linkError() const { return m_linkError; }
    // Stops the running image/texture work; the view keeps its last frame
    // until the next change asks for a new one.
    Q_INVOKABLE void cancelRendering();
    // {started, completed, rejected, canceled}: jobs begun, frames shown,
    // obsolete results discarded, jobs stopped by cancelRendering().
    Q_INVOKABLE QVariantMap renderStatistics() const;

    // Orbit by a delta (yaw wraps to −180…180, pitch clamps to ±90).
    Q_INVOKABLE void orbit(double deltaYaw, double deltaPitch);
    Q_INVOKABLE void resetView();
    // Continuous voxel index under an item point (see VolumeRaycaster::pick);
    // empty when the ray finds nothing. setCursorFromPick also moves the cursor.
    Q_INVOKABLE QVariantList pick(double x, double y) const;
    Q_INVOKABLE bool setCursorFromPick(double x, double y);
    // The exact reference image for the current settings at `width × height`.
    Q_INVOKABLE QImage renderReference(int width, int height) const;
    // JSON-safe view settings, validated and applied atomically on restore.
    Q_INVOKABLE QVariantMap saveSettings() const;
    Q_INVOKABLE bool restoreSettings(const QVariantMap &settings);
    VolumeRayParams rayParams() const { return m_params; }
    VolumeTransferTable transferTable() const;
signals:
    void sourceChanged();
    void settingsChanged();
    void appearanceChanged();
    void renderPathChanged();
    void errorChanged();
    void busyChanged();
    void linkChanged();
    void picked(const QVariantList &voxel);
protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void releaseResources() override;
private:
    void invalidate();
    void invalidateVolume();
    void fail(const QString &message);
    void publishPath(const QString &path, const QString &reason, const QSize &referenceSize);
    QString probeGpu() const;
    void refreshLinks();
    void startJob(VolumeRenderWorker::Kind kind, const QSize &size);
    void delivered(const VolumeRenderWorker::Result &result);
    void setBusy(bool busy);
    void waitForWorker();
    QPointer<VoxelVolume> m_volume;
    QPointer<VolumeCursor> m_cursor;
    QPointer<TransferFunctionModel> m_transfer;
    VolumeRayParams m_params;
    double m_windowCenter = 128, m_windowWidth = 256;
    QColor m_background = Qt::transparent;
    Renderer m_renderer = Automatic;
    qint64 m_maximumTextureBytes = 256 * 1024 * 1024;
    QString m_renderPath, m_fallbackReason, m_error;
    QSize m_referenceSize;
    QString m_linkError;
    bool m_transferUsable = true, m_cursorLinked = true;
    // AGENT-GUARD: the fields below are touched only in updatePaintNode (GUI
    // blocked) and on the GUI thread (worker deliveries, invalidation), never
    // by the worker, which gets copies in its Request.
    VolumeRenderWorker *m_worker = nullptr;
    quint64 m_generation = 1, m_volumeGeneration = 1;
    quint64 m_requestedGeneration = 0, m_requestedVolumeGeneration = 0;
    QSize m_requestedSize;
    QImage m_readyImage;       // a delivered, current reference image
    QByteArray m_packedBytes;  // a delivered, current texture pack
    QString m_packedType;
    int m_packedShape[3] = {0, 0, 0};
    bool m_imageReady = false, m_packReady = false;
    bool m_renderCanceled = false, m_busy = false;
    qint64 m_started = 0, m_completed = 0, m_rejected = 0, m_canceled = 0;
    bool m_textureDirty = true;
    bool m_contentIsGpu = false;
    bool m_packFailed = false;
};
} // namespace QindaTK
