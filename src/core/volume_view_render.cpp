// SPDX-License-Identifier: LGPL-3.0-or-later
// VolumeViewItem's scene-graph side: GPU capability probe, background jobs
// (D-261), link checks (D-260) and updatePaintNode. Properties, camera and
// settings live in volume_view_item.cpp.
#include "volume_render_node.h"
#include "volume_view_item.h"
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QRhiTexture::Format textureFormat(const QString &voxelType)
{
    if (voxelType == QLatin1String("uint16")) return QRhiTexture::R16;
    if (voxelType == QLatin1String("float32")) return QRhiTexture::R32F;
    return QRhiTexture::R8;
}
} // namespace

void VolumeViewItem::refreshLinks()
{
    QStringList reasons;
    const VoxelVolume *cursorVolume = m_cursor ? m_cursor->volume() : nullptr;
    const QString cursorReason = m_volume && cursorVolume && cursorVolume != m_volume
        ? m_volume->geometryMismatch(cursorVolume) : QString();
    if (!cursorReason.isEmpty()) reasons.append(QStringLiteral("Cursor not linked: %1").arg(cursorReason));
    QString transferReason;
    if (m_transfer && m_volume && !m_transfer->unit().isEmpty() && !m_volume->valueUnit().isEmpty()
        && m_transfer->unit() != m_volume->valueUnit())
        transferReason = QStringLiteral("The transfer function is in %1 but the volume stores %2")
                             .arg(m_transfer->unit(), m_volume->valueUnit());
    if (!transferReason.isEmpty()) reasons.append(transferReason);
    const bool transferUsable = transferReason.isEmpty();
    const QString linkError = reasons.join(QStringLiteral("; "));
    m_cursorLinked = cursorReason.isEmpty();
    if (transferUsable != m_transferUsable) { m_transferUsable = transferUsable; invalidate(); }
    if (linkError == m_linkError) return;
    m_linkError = linkError;
    emit linkChanged();
}

void VolumeViewItem::setBusy(bool busy)
{
    if (m_busy == busy) return;
    m_busy = busy;
    emit busyChanged();
}
void VolumeViewItem::cancelRendering()
{
    // Keeps the last frame; the next change (invalidate) asks for a new one.
    m_renderCanceled = true;
    m_worker->cancel();
}
QVariantMap VolumeViewItem::renderStatistics() const
{
    return {{QStringLiteral("started"), m_started}, {QStringLiteral("completed"), m_completed},
            {QStringLiteral("rejected"), m_rejected}, {QStringLiteral("canceled"), m_canceled}};
}

void VolumeViewItem::startJob(VolumeRenderWorker::Kind kind, const QSize &size)
{
    // Runs during sync (GUI blocked): the request copies everything the
    // worker needs, so the worker never reads item or model state.
    VolumeRenderWorker::Request request;
    request.kind = kind;
    request.volume = m_volume;
    request.params = m_params;
    request.size = size;
    request.voxelType = m_volume->voxelType();
    request.shape[0] = m_volume->sizeX(); request.shape[1] = m_volume->sizeY(); request.shape[2] = m_volume->sizeZ();
    if (kind == VolumeRenderWorker::Image) {
        request.generation = m_requestedGeneration = m_generation;
        m_requestedSize = size;
        request.table = transferTable();
        request.geometry = VolumeRayGeometry::compute(*m_volume, m_params, QSizeF(size));
    } else {
        request.generation = m_requestedVolumeGeneration = m_volumeGeneration;
    }
    ++m_started;
    m_worker->start(request);
    // AGENT-GUARD: sync runs on the render thread; signals go out queued.
    QMetaObject::invokeMethod(this, [this] { setBusy(m_worker->pending() > 0); }, Qt::QueuedConnection);
}

void VolumeViewItem::delivered(const VolumeRenderWorker::Result &result)
{
    setBusy(m_worker->pending() > 0);
    const bool image = result.kind == VolumeRenderWorker::Image;
    const bool current = image ? result.generation == m_generation && result.size == m_requestedSize
                               : result.generation == m_volumeGeneration;
    if (!current) { ++m_rejected; return; } // obsolete: never shown
    if (result.canceled) { ++m_canceled; return; }
    ++m_completed;
    if (image) {
        m_readyImage = result.image;
        m_imageReady = true;
    } else if (!result.ok) {
        m_packFailed = true; // probeGpu reports missing chunks; reference path follows
    } else {
        m_packedBytes = result.bytes;
        m_packedType = result.voxelType;
        std::copy(result.shape, result.shape + 3, m_packedShape);
        m_packReady = true;
    }
    update();
}

QString VolumeViewItem::probeGpu() const
{
    VolumeGpuProbe probe;
    QQuickWindow *win = window();
    const QSGRendererInterface *rif = win ? win->rendererInterface() : nullptr;
    QRhi *rhi = win ? win->rhi() : nullptr;
    probe.rhi = rif && QSGRendererInterface::isApiRhiBased(rif->graphicsApi()) && rhi;
    probe.shaders = VolumeRenderNode::shadersAvailable();
    if (rhi && m_volume) {
        probe.threeDimensionalTextures = rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures);
        probe.formatSupported = rhi->isTextureFormatSupported(textureFormat(m_volume->voxelType()),
                                                              QRhiTexture::ThreeDimensional);
        probe.maximumTextureSide = rhi->resourceLimit(QRhi::TextureSizeMax);
    }
    QString reason = VolumeRaycaster::gpuUnavailableReason(m_volume, probe, m_maximumTextureBytes,
                                                           m_renderer == Reference);
    if (reason.isEmpty() && m_volume && (m_params.channel >= m_volume->channels()
                                         || m_params.frame >= m_volume->frames()))
        reason = QStringLiteral("The channel or frame is outside the volume");
    if (reason.isEmpty() && m_packFailed) reason = QStringLiteral("The volume has missing chunks");
    return reason;
}
void VolumeViewItem::publishPath(const QString &path, const QString &reason, const QSize &referenceSize)
{
    if (path == m_renderPath && reason == m_fallbackReason && referenceSize == m_referenceSize) return;
    // AGENT-GUARD: called during sync on the render thread; the GUI-thread
    // state and its signal are updated by a queued call, never emitted here.
    QMetaObject::invokeMethod(this, [this, path, reason, referenceSize] {
        if (path == m_renderPath && reason == m_fallbackReason && referenceSize == m_referenceSize) return;
        m_renderPath = path;
        m_fallbackReason = reason;
        m_referenceSize = referenceSize;
        emit renderPathChanged();
    }, Qt::QueuedConnection);
}

QSGNode *VolumeViewItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QQuickWindow *win = window();
    if (!win || width() <= 0 || height() <= 0) {
        delete old;
        m_contentIsGpu = false;
        return nullptr;
    }
    QSGNode *root = old;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(win->createRectangleNode());
        m_contentIsGpu = false;
        m_textureDirty = true;
    }
    auto *background = static_cast<QSGRectangleNode *>(root->firstChild());
    background->setRect(boundingRect());
    background->setColor(m_background);
    QSGNode *content = background->nextSibling();
    const auto replaceContent = [&](QSGNode *next) {
        if (content) { root->removeChildNode(content); delete content; }
        content = next;
        if (next) root->appendChildNode(next);
    };

    const QString reason = probeGpu();
    if (reason.isEmpty()) {
        if (!m_contentIsGpu || !content) {
            replaceContent(new VolumeRenderNode(win));
            m_contentIsGpu = true;
            m_textureDirty = true;
            if (!m_packReady) m_requestedVolumeGeneration = 0; // a fresh node needs a fresh pack
        }
        auto *node = static_cast<VolumeRenderNode *>(content);
        if (m_textureDirty && m_packReady) {
            node->setVolumeData(std::move(m_packedBytes), m_packedType, m_packedShape[0],
                                m_packedShape[1], m_packedShape[2]);
            m_packedBytes = {};
            m_packReady = false;
            m_textureDirty = false;
        } else if (m_textureDirty) {
            // Packing reads the whole channel/frame: never here (D-261).
            node->clearVolume();
            if (m_requestedVolumeGeneration != m_volumeGeneration && !m_renderCanceled)
                startJob(VolumeRenderWorker::Texture, {});
        }
        node->setFrame(VolumeRayGeometry::compute(*m_volume, m_params, size()), transferTable(),
                       m_params.mode, m_params.interpolation == VoxelVolume::Linear, size());
        node->markDirty(QSGNode::DirtyMaterial);
        publishPath(QStringLiteral("gpu"), {}, {});
        return root;
    }

    // Reference path: the exact CPU image, capped at MaximumReferencePixels,
    // rendered by the worker; the last completed frame stays up meanwhile.
    const double factor = std::min(1.0, std::sqrt(MaximumReferencePixels / (width() * height())));
    const QSize referenceSize(std::max(1, int(std::lround(width() * factor))),
                              std::max(1, int(std::lround(height() * factor))));
    bool upload = false;
    if (m_contentIsGpu || !content) {
        auto *image = win->createImageNode();
        image->setOwnsTexture(true);
        replaceContent(image);
        m_contentIsGpu = false;
        m_requestedGeneration = 0; // a fresh node needs a fresh image
    }
    if (!m_volume) {
        m_readyImage = QImage(referenceSize, QImage::Format_ARGB32_Premultiplied);
        m_readyImage.fill(Qt::transparent);
        m_imageReady = true;
        m_requestedGeneration = m_generation;
        m_requestedSize = referenceSize;
    } else if ((m_requestedGeneration != m_generation || m_requestedSize != referenceSize) && !m_renderCanceled) {
        startJob(VolumeRenderWorker::Image, referenceSize);
    }
    auto *image = static_cast<QSGImageNode *>(content);
    if (m_imageReady && !m_readyImage.isNull()) {
        if (QSGTexture *texture = win->createTextureFromImage(m_readyImage, QQuickWindow::TextureHasAlphaChannel)) {
            image->setTexture(texture); // the node deletes the texture it replaces
            upload = true;
        }
        m_imageReady = false;
    }
    if (!image->texture()) {
        // Nothing rendered yet: a transparent pixel keeps the node valid.
        QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
        empty.fill(Qt::transparent);
        image->setTexture(win->createTextureFromImage(empty, QQuickWindow::TextureHasAlphaChannel));
    }
    image->setRect(boundingRect());
    const QSize shown = upload ? m_readyImage.size() : image->texture()->textureSize();
    const bool exact = shown == QSize(int(std::lround(width())), int(std::lround(height())));
    image->setFiltering(exact ? QSGTexture::Nearest : QSGTexture::Linear);
    publishPath(QStringLiteral("reference"), reason, referenceSize);
    return root;
}
} // namespace QindaTK
