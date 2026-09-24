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
class ImageSliceItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(VoxelVolume *volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(VolumeCursor *cursor READ cursor WRITE setCursor NOTIFY changed)
    Q_PROPERTY(TransferFunctionModel *transfer READ transfer WRITE setTransfer NOTIFY changed)
    Q_PROPERTY(Plane plane READ plane WRITE setPlane NOTIFY changed)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY changed)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY changed)
    Q_PROPERTY(VoxelVolume::Interpolation interpolation READ interpolation WRITE setInterpolation NOTIFY changed)
    Q_PROPERTY(double windowCenter READ windowCenter WRITE setWindowCenter NOTIFY changed)
    Q_PROPERTY(double windowWidth READ windowWidth WRITE setWindowWidth NOTIFY changed)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY changed)
    Q_PROPERTY(QColor crosshairColor READ crosshairColor WRITE setCrosshairColor NOTIFY changed)
    Q_PROPERTY(double crosshairWidth READ crosshairWidth WRITE setCrosshairWidth NOTIFY changed)
public:
    enum Plane { Axial = 0, Coronal = 1, Sagittal = 2 };
    Q_ENUM(Plane)
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
    mutable QImage m_image;
    mutable bool m_dirty = true;
};
} // namespace QindaTK
