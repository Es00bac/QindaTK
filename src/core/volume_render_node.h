// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "volume_raycast.h"
#include <QByteArray>
#include <QPointer>
#include <QSGRenderNode>
#include <QVector>
#include <memory>

class QQuickWindow;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

namespace QindaTK {
// GPU half of D-146: the packed voxels as one 3D texture, the transfer table
// as a 256×1 texture and a full-item quad whose fragment shader
// (src/shaders/volume_raycast.frag) marches the same rays as VolumeRaycaster.
// AGENT-GUARD: every QRhi resource lives in this node and is created and
// released on the render thread (prepare/releaseResources/destructor). The
// item hands over plain data during sync only; it never holds a GPU handle.
class VolumeRenderNode : public QSGRenderNode {
public:
    static QString vertexShaderPath();
    static QString fragmentShaderPath();
    static bool shadersAvailable();
    explicit VolumeRenderNode(QQuickWindow *window);
    ~VolumeRenderNode() override;
    // Tightly packed voxels from VolumeRaycaster::packTexture; voxelType is
    // "uint8", "uint16" or "float32". Uploaded (and dropped) on next prepare.
    void setVolumeData(QByteArray bytes, const QString &voxelType, int sizeX, int sizeY, int sizeZ);
    void setFrame(const VolumeRayGeometry &geometry, const VolumeTransferTable &table,
                  VolumeRenderMode mode, bool linear, const QSizeF &size);
    void prepare() override;
    void render(const RenderState *state) override;
    void releaseResources() override;
    RenderingFlags flags() const override;
    QRectF rect() const override;
    StateFlags changedStates() const override;
private:
    static constexpr int UniformFloats = 16 + 8 * 4; // mat4 + vec4[8], std140
    QPointer<QQuickWindow> m_window;
    QByteArray m_pending;
    QString m_voxelType = QStringLiteral("uint8");
    int m_size[3] = {0, 0, 0};
    int m_textureSize[3] = {0, 0, 0};
    QString m_textureType;
    bool m_linear = true, m_samplerLinear = true;
    bool m_tableDirty = true;
    VolumeRayGeometry m_geometry;
    VolumeTransferTable m_table;
    VolumeRenderMode m_mode = VolumeRenderMode::Composite;
    QSizeF m_itemSize;
    QVector<quint32> m_passFormat;
    std::unique_ptr<QRhiBuffer> m_vertexBuffer, m_uniformBuffer;
    std::unique_ptr<QRhiTexture> m_volumeTexture, m_transferTexture;
    std::unique_ptr<QRhiSampler> m_volumeSampler, m_transferSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
};
} // namespace QindaTK
