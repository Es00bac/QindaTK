// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "voxel_volume.h"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// One cursor shared by axial, coronal and sagittal ImageSliceView instances.
// The point is in continuous voxel indices of `volume`, clamped to the outer
// voxel centres; worldPoint applies the volume's spacing/origin/orientation.
class VolumeCursor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::VoxelVolume *volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(double x READ x NOTIFY changed)
    Q_PROPERTY(double y READ y NOTIFY changed)
    Q_PROPERTY(double z READ z NOTIFY changed)
    Q_PROPERTY(QVariantList worldPoint READ worldPoint NOTIFY changed)
public:
    explicit VolumeCursor(QObject *parent = nullptr);
    VoxelVolume *volume() const { return m_volume; }
    void setVolume(VoxelVolume *volume);
    double x() const { return m_point[0]; }
    double y() const { return m_point[1]; }
    double z() const { return m_point[2]; }
    QVariantList worldPoint() const;
    Q_INVOKABLE bool setVoxelPoint(double x, double y, double z);
    Q_INVOKABLE bool setWorldPoint(double x, double y, double z);
signals:
    void changed();
private:
    void volumeChanged();
    void recenter();
    void publish();
    QPointer<VoxelVolume> m_volume;
    double m_point[3] = {0, 0, 0};
    int m_shape[3] = {0, 0, 0};
    QVariantList m_world;
};
} // namespace QindaTK
