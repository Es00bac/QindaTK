// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "voxel_volume.h"
#include "volume_cursor.h"
#include "transfer_function_model.h"
#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Paints one principal voxel-index plane of a VoxelVolume, fitted by physical
// spacing, or an oblique world-space reslice (plane Oblique), with the shared
// VolumeCursor crosshair.
// AGENT-CONTRACT (docs/volume-imaging.md, D-086): the target rectangle spans
// whole voxel cells (size × spacing), so its edges are continuous indices
// −0.5 and size−0.5. sliceImage, voxelAt, positionForVoxel and the crosshair
// share that one mapping; change them together. Oblique planes use the square
// reslice frame of D-149 (image_slice_reslice.cpp) in all four instead.
// AGENT-CONTRACT (docs/volume-imaging.md#linked-views-and-overlays, D-260):
// a cursor bound to another volume, an overlay, and a transfer function are
// used only when compatible (VoxelVolume::geometryMismatch, matching value
// units); otherwise the view says why in linkError/overlayError and ignores
// them instead of drawing a wrong point or colour.
class ImageSliceItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    // AGENT-NOTE: namespace-qualified property types let qmltyperegistrar
    // match the registered QindaTK:: names, so qmllint can type the
    // ImageSliceView aliases; unqualified names leave them unresolved.
    Q_PROPERTY(QindaTK::VoxelVolume *volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(QindaTK::VolumeCursor *cursor READ cursor WRITE setCursor NOTIFY changed)
    Q_PROPERTY(QindaTK::TransferFunctionModel *transfer READ transfer WRITE setTransfer NOTIFY changed)
    Q_PROPERTY(Plane plane READ plane WRITE setPlane NOTIFY changed)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY changed)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY changed)
    Q_PROPERTY(QindaTK::VoxelVolume::Interpolation interpolation READ interpolation WRITE setInterpolation NOTIFY changed)
    Q_PROPERTY(double windowCenter READ windowCenter WRITE setWindowCenter NOTIFY changed)
    Q_PROPERTY(double windowWidth READ windowWidth WRITE setWindowWidth NOTIFY changed)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY changed)
    Q_PROPERTY(QColor crosshairColor READ crosshairColor WRITE setCrosshairColor NOTIFY changed)
    Q_PROPERTY(double crosshairWidth READ crosshairWidth WRITE setCrosshairWidth NOTIFY changed)
    Q_PROPERTY(QVariantList resliceOrientation READ resliceOrientation WRITE setResliceOrientation NOTIFY changed)
    Q_PROPERTY(QindaTK::VoxelVolume *overlay READ overlay WRITE setOverlay NOTIFY changed)
    Q_PROPERTY(QindaTK::TransferFunctionModel *overlayTransfer READ overlayTransfer WRITE setOverlayTransfer NOTIFY changed)
    Q_PROPERTY(double overlayOpacity READ overlayOpacity WRITE setOverlayOpacity NOTIFY changed)
    Q_PROPERTY(QString linkError READ linkError NOTIFY linkChanged)
    Q_PROPERTY(bool cursorLinked READ cursorLinked NOTIFY linkChanged)
    Q_PROPERTY(QString overlayError READ overlayError NOTIFY linkChanged)
    Q_PROPERTY(int sliceCount READ sliceCount NOTIFY sliceChanged)
    Q_PROPERTY(int sliceIndex READ sliceIndex NOTIFY sliceChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    enum Plane { Axial = 0, Coronal = 1, Sagittal = 2, Oblique = 3 };
    Q_ENUM(Plane)
    // AGENT-GUARD: the generated slice image is capped per side and scaled
    // into the target rectangle; lifting the cap makes one repaint cost up to
    // eight samples for every pixel of an arbitrarily large item.
    static constexpr int MaximumImageSide = 2048;
    explicit ImageSliceItem(QQuickItem *parent = nullptr);
    VoxelVolume *volume() const { return m_volume; }
    void setVolume(VoxelVolume *volume);
    VolumeCursor *cursor() const { return m_cursor; }
    void setCursor(VolumeCursor *cursor);
    TransferFunctionModel *transfer() const { return m_transfer; }
    void setTransfer(TransferFunctionModel *transfer);
    Plane plane() const { return m_plane; }
    void setPlane(Plane plane);
    int channel() const { return m_channel; }
    void setChannel(int channel);
    int frame() const { return m_frame; }
    void setFrame(int frame);
    VoxelVolume::Interpolation interpolation() const { return m_interpolation; }
    void setInterpolation(VoxelVolume::Interpolation interpolation);
    double windowCenter() const { return m_windowCenter; }
    void setWindowCenter(double value);
    double windowWidth() const { return m_windowWidth; }
    void setWindowWidth(double value);
    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &color);
    QColor crosshairColor() const { return m_crosshair; }
    void setCrosshairColor(const QColor &color);
    double crosshairWidth() const { return m_crosshairWidth; }
    void setCrosshairWidth(double width);
    // Row-major 3×3 orthonormal world matrix whose columns are the oblique
    // image's right, down and normal directions (identity: world X right,
    // world Y down, normal world Z). Rejected matrices set lastError.
    QVariantList resliceOrientation() const;
    void setResliceOrientation(const QVariantList &matrix);
    QString lastError() const { return m_error; }
    // An overlay volume on the same voxel grid (a label map, a second
    // modality) drawn through overlayTransfer at overlayOpacity (0–1).
    VoxelVolume *overlay() const { return m_overlay; }
    void setOverlay(VoxelVolume *overlay);
    TransferFunctionModel *overlayTransfer() const { return m_overlayTransfer; }
    void setOverlayTransfer(TransferFunctionModel *transfer);
    double overlayOpacity() const { return m_overlayOpacity; }
    void setOverlayOpacity(double opacity);
    // Why the cursor or transfer function is ignored (empty when both apply),
    // and why the overlay is not drawn (empty when it is, or when unset).
    QString linkError() const { return m_linkError; }
    QString overlayError() const { return m_overlayError; }
    bool cursorLinked() const;
    // JSON-safe view settings (plane, reslice orientation, window, channel,
    // frame, interpolation, overlay opacity) with the volume identity; restore
    // validates atomically and rejects settings for another dataset (D-260).
    Q_INVOKABLE QVariantMap saveSettings() const;
    Q_INVOKABLE bool restoreSettings(const QVariantMap &settings);
    // Principal planes count voxel slices; an oblique plane counts steps of
    // the smallest spacing along its normal across the volume diagonal.
    int sliceCount() const;
    int sliceIndex() const;
    // Moves the shared cursor `steps` slices along the plane normal, or by
    // (dh, dv) within the plane (voxels, or smallest-spacing steps when
    // oblique). False without a cursor or when clamping leaves it in place.
    Q_INVOKABLE bool stepSlice(int steps);
    Q_INVOKABLE bool moveCrosshair(int dh, int dv);
    Q_INVOKABLE QVariantList voxelAt(double px, double py) const;
    Q_INVOKABLE QVariantList positionForVoxel(double x, double y, double z) const;
    QImage sliceImage(int columns, int rows) const;
    void paint(QPainter *painter) override;
signals:
    void changed();
    void sliceChanged();
    void linkChanged();
private:
    // D-149: an oblique plane passes through the cursor, is centred on the
    // volume centre's projection and spans the volume diagonal on each side.
    struct ObliqueFrame {
        bool valid = false;
        double u[3], v[3], w[3];
        double centre[3];      // world point at the image centre
        double side = 0;       // world units across the square image
        double step = 1;       // smallest voxel spacing
        double offset = 0;     // signed distance of the plane from the volume centre
        double voxelCentre[3]; // continuous index of `centre`
        double voxelU[3], voxelV[3]; // index delta per world unit along u, v
    };
    ObliqueFrame obliqueFrame() const;
    QVariantList obliqueVoxelAt(const QRectF &rect, double px, double py) const;
    QVariantList obliquePosition(const QRectF &rect, double x, double y, double z) const;
    QImage obliqueImage(int columns, int rows) const;
    bool moveCursorWorld(const double delta[3]);
    int horizontalSize() const;
    int verticalSize() const;
    double horizontalSpacing() const;
    double verticalSpacing() const;
    double currentSlice() const;
    QRectF targetRect() const;
    QRgb colorFor(double value) const;
    // Base colour at a voxel point with the accepted overlay composited over it.
    QRgb shadeAt(double x, double y, double z) const;
    void refreshLinks();
    static bool parseOrientation(const QVariantList &matrix, double out[9]);
    void invalidate();
    QPointer<VoxelVolume> m_volume;
    QPointer<VolumeCursor> m_cursor;
    QPointer<TransferFunctionModel> m_transfer;
    Plane m_plane = Axial;
    int m_channel = 0, m_frame = 0;
    VoxelVolume::Interpolation m_interpolation = VoxelVolume::Nearest;
    double m_windowCenter = 128, m_windowWidth = 256;
    QColor m_background = Qt::transparent, m_crosshair = Qt::transparent;
    double m_crosshairWidth = 0;
    double m_reslice[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    QString m_error, m_linkError, m_overlayError;
    QPointer<VoxelVolume> m_overlay;
    QPointer<TransferFunctionModel> m_overlayTransfer;
    double m_overlayOpacity = 0.5;
    bool m_cursorLinked = true, m_transferUsable = true, m_overlayUsable = false;
    QImage m_image;
    double m_imageSlice = 0;
    bool m_dirty = true;
};
} // namespace QindaTK
