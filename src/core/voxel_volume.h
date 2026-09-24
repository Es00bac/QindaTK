// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {

// Host source chunks are little-endian, tightly packed X-fastest 32³ blocks.
// A failed read returns an empty block, which the volume treats as missing.
class VoxelChunkSource {
public:
    virtual ~VoxelChunkSource() = default;
    virtual QByteArray readChunk(int cx, int cy, int cz, int channel, int frame) = 0;
};

class VoxelVolume : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int sizeX READ sizeX NOTIFY changed)
    Q_PROPERTY(int sizeY READ sizeY NOTIFY changed)
    Q_PROPERTY(int sizeZ READ sizeZ NOTIFY changed)
    Q_PROPERTY(int channels READ channels NOTIFY changed)
    Q_PROPERTY(int frames READ frames NOTIFY changed)
    Q_PROPERTY(QString voxelType READ voxelType NOTIFY changed)
    Q_PROPERTY(QString unit READ unit NOTIFY changed)
    Q_PROPERTY(QString valueUnit READ valueUnit NOTIFY changed)
    Q_PROPERTY(QVariantList spacing READ spacing NOTIFY changed)
    Q_PROPERTY(QVariantList origin READ origin NOTIFY changed)
    Q_PROPERTY(QVariantList orientation READ orientation NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(qint64 residentBytes READ residentBytes NOTIFY changed)
    Q_PROPERTY(qint64 maximumResidentBytes READ maximumResidentBytes WRITE setMaximumResidentBytes NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Interpolation { Nearest = 0, Linear = 1 };
    Q_ENUM(Interpolation)
    explicit VoxelVolume(QObject *parent = nullptr);
    int sizeX() const { return m_shape[0]; }
    int sizeY() const { return m_shape[1]; }
    int sizeZ() const { return m_shape[2]; }
    int channels() const { return m_channels; }
    int frames() const { return m_frames; }
    QString voxelType() const { return m_type; }
    QString unit() const { return m_unit; }
    QString valueUnit() const { return m_valueUnit; }
    QVariantList spacing() const;
    QVariantList origin() const;
    QVariantList orientation() const;
    quint64 revision() const { return m_revision; }
    qint64 residentBytes() const { return m_residentBytes; }
    qint64 maximumResidentBytes() const { return m_maxResidentBytes; }
    void setMaximumResidentBytes(qint64 bytes);
    QString lastError() const { return m_error; }
    Q_INVOKABLE bool configure(const QVariantMap &description);
    Q_INVOKABLE bool setVoxel(int x, int y, int z, double value, int channel = 0, int frame = 0);
    Q_INVOKABLE bool fillSpherePhantom(double radiusFraction = 0.32);
    Q_INVOKABLE QVariantMap sample(double x, double y, double z, int channel = 0,
                                    int frame = 0, Interpolation interpolation = Nearest) const;
    Q_INVOKABLE QVariantList worldFromVoxel(double x, double y, double z) const;
    Q_INVOKABLE QVariantList voxelFromWorld(double x, double y, double z) const;
    bool putChunk(int cx, int cy, int cz, int channel, int frame, const QByteArray &bytes);
    void setSource(std::shared_ptr<VoxelChunkSource> source);
    double valueAt(int x, int y, int z, int channel = 0, int frame = 0) const;
    double sampleValue(double x, double y, double z, int channel, int frame,
                       Interpolation interpolation) const;
signals:
    void changed();
    void errorChanged();
private:
    static constexpr int Chunk = 32;
    bool validIndex(int x, int y, int z, int channel, int frame) const;
    int bytesPerVoxel() const;
    qsizetype chunkBytes() const;
    quint64 key(int cx, int cy, int cz, int channel, int frame) const;
    const QByteArray *chunkForRead(int cx, int cy, int cz, int channel, int frame) const;
    bool writeVoxel(int x, int y, int z, double value, int channel, int frame);
    void fail(const QString &message);
    int m_shape[3] = {0, 0, 0};
    int m_channels = 1, m_frames = 1;
    QString m_type = QStringLiteral("uint8");
    QString m_unit = QStringLiteral("mm"), m_valueUnit;
    double m_spacing[3] = {1, 1, 1};
    double m_origin[3] = {0, 0, 0};
    double m_orientation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    QHash<quint64, QByteArray> m_blocks;
    mutable QHash<quint64, QByteArray> m_cache;
    mutable QList<quint64> m_cacheOrder;
    std::shared_ptr<VoxelChunkSource> m_source;
    mutable qint64 m_residentBytes = 0;
    qint64 m_maxResidentBytes = 64 * 1024 * 1024;
    quint64 m_revision = 0;
    QString m_error;
};

} // namespace QindaTK
