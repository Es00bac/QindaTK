// SPDX-License-Identifier: LGPL-3.0-or-later
#include "volume_render_node.h"
#include <QFile>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <algorithm>
#include <cstring>

namespace QindaTK {
namespace {
QShader loadShader(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}
QRhiTexture::Format formatFor(const QString &voxelType)
{
    if (voxelType == QLatin1String("uint16")) return QRhiTexture::R16;
    if (voxelType == QLatin1String("float32")) return QRhiTexture::R32F;
    return QRhiTexture::R8;
}
// Normalized texture reads return value / type maximum; float32 is exact.
float valueScaleFor(const QString &voxelType)
{
    if (voxelType == QLatin1String("uint16")) return 65535.0f;
    if (voxelType == QLatin1String("float32")) return 1.0f;
    return 255.0f;
}
int bytesPerVoxel(const QString &voxelType)
{
    return voxelType == QLatin1String("uint16") ? 2 : voxelType == QLatin1String("float32") ? 4 : 1;
}
void putVec4(float *out, const double xyz[3], double w)
{
    out[0] = float(xyz[0]); out[1] = float(xyz[1]); out[2] = float(xyz[2]); out[3] = float(w);
}
} // namespace

// AGENT-CONTRACT (src/CMakeLists.txt qt_add_shaders PREFIX "/qindatk"): these
// resource paths are where the build puts the compiled .qsb files.
QString VolumeRenderNode::vertexShaderPath() { return QStringLiteral(":/qindatk/shaders/volume_raycast.vert.qsb"); }
QString VolumeRenderNode::fragmentShaderPath() { return QStringLiteral(":/qindatk/shaders/volume_raycast.frag.qsb"); }
bool VolumeRenderNode::shadersAvailable()
{
    return QFile::exists(vertexShaderPath()) && QFile::exists(fragmentShaderPath());
}
VolumeRenderNode::VolumeRenderNode(QQuickWindow *window) : m_window(window) {}
VolumeRenderNode::~VolumeRenderNode() { releaseResources(); }

void VolumeRenderNode::setVolumeData(QByteArray bytes, const QString &voxelType, int sizeX, int sizeY, int sizeZ)
{
    m_pending = std::move(bytes);
    m_voxelType = voxelType;
    m_size[0] = sizeX; m_size[1] = sizeY; m_size[2] = sizeZ;
}
void VolumeRenderNode::setFrame(const VolumeRayGeometry &geometry, const VolumeTransferTable &table,
                                VolumeRenderMode mode, bool linear, const QSizeF &size)
{
    m_geometry = geometry;
    if (table.rgba != m_table.rgba) m_tableDirty = true;
    m_table = table;
    m_mode = mode;
    m_linear = linear;
    m_itemSize = size;
}
void VolumeRenderNode::releaseResources()
{
    m_pipeline.reset();
    m_bindings.reset();
    m_volumeSampler.reset();
    m_transferSampler.reset();
    m_volumeTexture.reset();
    m_transferTexture.reset();
    m_uniformBuffer.reset();
    m_vertexBuffer.reset();
    m_textureSize[0] = m_textureSize[1] = m_textureSize[2] = 0;
    m_passFormat.clear();
    m_tableDirty = true;
}
QSGRenderNode::RenderingFlags VolumeRenderNode::flags() const
{
    // Draws only inside rect() through QRhi, with no direct graphics-API calls.
    return BoundedRectRendering | NoExternalRendering;
}
QRectF VolumeRenderNode::rect() const { return QRectF(QPointF(0, 0), m_itemSize); }
QSGRenderNode::StateFlags VolumeRenderNode::changedStates() const { return ViewportState | ScissorState; }

void VolumeRenderNode::prepare()
{
    QRhi *rhi = m_window ? m_window->rhi() : nullptr;
    if (!rhi || !m_geometry.valid) return;
    QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
    bool bindingsDirty = !m_bindings;
    if (!m_vertexBuffer) {
        m_vertexBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 8 * sizeof(float)));
        m_uniformBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                             UniformFloats * sizeof(float)));
        m_transferTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(VolumeTransferTable::Size, 1)));
        m_transferSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_vertexBuffer->create() || !m_uniformBuffer->create() || !m_transferTexture->create()
            || !m_transferSampler->create()) {
            updates->release();
            releaseResources();
            return;
        }
        bindingsDirty = true;
    }
    if (!m_pending.isEmpty()) {
        const bool reshaped = !std::equal(m_size, m_size + 3, m_textureSize) || m_textureType != m_voxelType;
        if (reshaped) {
            m_volumeTexture.reset(rhi->newTexture(formatFor(m_voxelType), m_size[0], m_size[1], m_size[2],
                                                  1, QRhiTexture::ThreeDimensional));
            if (!m_volumeTexture->create()) {
                m_volumeTexture.reset();
                updates->release();
                return;
            }
            std::copy(m_size, m_size + 3, m_textureSize);
            m_textureType = m_voxelType;
            bindingsDirty = true;
        }
        // For a 3D texture each upload entry's layer is one z slice.
        const quint32 sliceBytes = quint32(m_size[0]) * quint32(m_size[1]) * quint32(bytesPerVoxel(m_voxelType));
        QVector<QRhiTextureUploadEntry> entries;
        entries.reserve(m_size[2]);
        for (int z = 0; z < m_size[2]; ++z)
            entries.append(QRhiTextureUploadEntry(z, 0, QRhiTextureSubresourceUploadDescription(
                m_pending.constData() + qsizetype(z) * sliceBytes, sliceBytes)));
        QRhiTextureUploadDescription description;
        description.setEntries(entries.cbegin(), entries.cend());
        updates->uploadTexture(m_volumeTexture.get(), description);
        m_pending.clear(); // the upload copied the slices; drop the staging copy
    }
    if (!m_volumeTexture) { updates->release(); return; }
    if (!m_volumeSampler || m_samplerLinear != m_linear) {
        const QRhiSampler::Filter filter = m_linear ? QRhiSampler::Linear : QRhiSampler::Nearest;
        // ClampToEdge + Linear is D-086's clamped trilinear sample.
        m_volumeSampler.reset(rhi->newSampler(filter, filter, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                              QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_volumeSampler->create();
        m_samplerLinear = m_linear;
        bindingsDirty = true;
    }
    if (m_tableDirty) {
        updates->uploadTexture(m_transferTexture.get(), m_table.image());
        m_tableDirty = false;
    }
    const QVector<quint32> passFormat = renderTarget()->renderPassDescriptor()->serializedFormat();
    if (bindingsDirty) {
        m_bindings.reset(rhi->newShaderResourceBindings());
        const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, stages, m_uniformBuffer.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      m_volumeTexture.get(), m_volumeSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                      m_transferTexture.get(), m_transferSampler.get())});
        m_bindings->create();
        m_pipeline.reset(); // rebuilt against the new bindings below
    }
    if (!m_pipeline || passFormat != m_passFormat) {
        m_pipeline.reset(rhi->newGraphicsPipeline());
        QRhiGraphicsPipeline::TargetBlend blend; // premultiplied source over destination
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_pipeline->setTargetBlends({blend});
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
        m_pipeline->setSampleCount(renderTarget()->sampleCount());
        m_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader(vertexShaderPath())},
            {QRhiShaderStage::Fragment, loadShader(fragmentShaderPath())}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{2 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
        m_pipeline->setVertexInputLayout(layout);
        m_pipeline->setShaderResourceBindings(m_bindings.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        if (!m_pipeline->create()) {
            m_pipeline.reset();
            updates->release();
            return;
        }
        m_passFormat = passFormat;
    }
    const float w = float(m_itemSize.width()), h = float(m_itemSize.height());
    const float vertices[8] = {0, 0, w, 0, 0, h, w, h};
    updates->updateDynamicBuffer(m_vertexBuffer.get(), 0, sizeof(vertices), vertices);
    // AGENT-CONTRACT (src/shaders/volume_raycast.frag `buf`): the order and
    // meaning of these eight vec4s is the shader's uniform block.
    float uniforms[UniformFloats];
    const QMatrix4x4 mvp = *projectionMatrix() * *matrix();
    std::memcpy(uniforms, mvp.constData(), 16 * sizeof(float));
    const VolumeRayGeometry &g = m_geometry;
    // The shader rebuilds pixel offsets from the item size, the geometry was
    // computed for; both are logical item pixels.
    putVec4(uniforms + 16, g.centre, inheritedOpacity());
    putVec4(uniforms + 20, g.right, g.width);
    putVec4(uniforms + 24, g.down, g.height);
    putVec4(uniforms + 28, g.direction, g.step);
    putVec4(uniforms + 32, g.boxLow, g.exponent);
    putVec4(uniforms + 36, g.boxHigh, m_mode == VolumeRenderMode::MaximumIntensity ? 1 : 0);
    putVec4(uniforms + 40, g.sizeInverse, valueScaleFor(m_textureType));
    uniforms[44] = float(m_table.domainMin);
    uniforms[45] = float(1.0 / (m_table.domainMax - m_table.domainMin));
    uniforms[46] = float(VolumeRaycaster::TerminationOpacity);
    uniforms[47] = float(g.maximumSteps);
    updates->updateDynamicBuffer(m_uniformBuffer.get(), 0, sizeof(uniforms), uniforms);
    commandBuffer()->resourceUpdate(updates);
}

void VolumeRenderNode::render(const RenderState *state)
{
    if (!m_pipeline || !m_bindings || !m_volumeTexture) return;
    QRhiCommandBuffer *cb = commandBuffer();
    cb->setGraphicsPipeline(m_pipeline.get());
    const QSize target = renderTarget()->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, target.width(), target.height()));
    if (state && state->scissorEnabled()) {
        const QRect clip = state->scissorRect();
        cb->setScissor(QRhiScissor(clip.x(), clip.y(), clip.width(), clip.height()));
    } else {
        cb->setScissor(QRhiScissor(0, 0, target.width(), target.height()));
    }
    cb->setShaderResources(m_bindings.get());
    const QRhiCommandBuffer::VertexInput input(m_vertexBuffer.get(), 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(4);
}
} // namespace QindaTK
