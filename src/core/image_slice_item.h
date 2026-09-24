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
// spacing, with the shared VolumeCursor crosshair.
// AGENT-CONTRACT (docs/volume-imaging.md, D-086): the target rectangle spans
// whole voxel cells (size × spacing), so its edges are continuous indices
// −0.5 and size−0.5. sliceImage, voxelAt, positionForVoxel and the crosshair
// share that one mapping; change them together.
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
public:
    enum Plane { Axial = 0, Coronal = 1, Sagittal = 2 };
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
    Q_INVOKABLE QVariantList voxelAt(double px, double py) const;
    Q_INVOKABLE QVariantList positionForVoxel(double x, double y, double z) const;
    QImage sliceImage(int columns, int rows) const;
    void paint(QPainter *painter) override;
signals:
    void changed();
private:
    int horizontalSize() const;
    int verticalSize() const;
    double horizontalSpacing() const;
    double verticalSpacing() const;
    double currentSlice() const;
    QRectF targetRect() const;
    QRgb colorFor(double value) const;
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
    QImage m_image;
    double m_imageSlice = 0;
    bool m_dirty = true;
};
} // namespace QindaTK
