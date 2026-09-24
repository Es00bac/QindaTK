// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_frame_view.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <QSGTexture>

namespace QindaTK {

MediaFrameView::MediaFrameView(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void MediaFrameView::setPlayer(MediaPlayer *player)
{
    if (m_player == player) return;
    disconnect(m_link);
    m_player = player;
    if (player) m_link = connect(player, &MediaPlayer::frameChanged, this, &MediaFrameView::takeFrame);
    m_playerRevision = 0;
    takeFrame();
    emit playerChanged();
}

void MediaFrameView::takeFrame()
{
    if (m_player && m_player->presentedRevision() == m_playerRevision && m_playerRevision != 0) return;
    const QImage image = m_player ? m_player->presentedImage() : QImage();
    // Conversion happens here on the GUI thread, never during sync.
    m_image = image.hasAlphaChannel() ? image.convertToFormat(QImage::Format_ARGB32_Premultiplied) : image;
    m_frame = m_player && !image.isNull() ? m_player->engine()->presentedFrame() : -1;
    m_key = m_player && !image.isNull() ? m_player->presentedFrameKey() : QString();
    m_pixelAspect = m_player ? m_player->pixelAspect() : 1.0;
    m_playerRevision = m_player ? m_player->presentedRevision() : 0;
    ++m_revision;
    emit displayedChanged();
    update();
}

QRectF MediaFrameView::frameRect() const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) return {};
    QSizeF display(m_image.width() * m_pixelAspect, m_image.height());
    display.scale(width(), height(), Qt::KeepAspectRatio);
    return QRectF((width() - display.width()) / 2.0, (height() - display.height()) / 2.0, display.width(),
                  display.height());
}

void MediaFrameView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        emit displayedChanged();
        update();
    }
}

QSGNode *MediaFrameView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGImageNode *>(oldNode);
    const QRectF target = frameRect();
    if (m_image.isNull() || target.isEmpty() || !window()) {
        delete node;
        m_uploadedRevision = 0;
        return nullptr;
    }
    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        m_uploadedRevision = 0;
    }
    if (m_uploadedRevision != m_revision) {
        const QQuickWindow::CreateTextureOptions options = m_image.hasAlphaChannel()
            ? QQuickWindow::TextureHasAlphaChannel : QQuickWindow::CreateTextureOptions();
        QSGTexture *texture = window()->createTextureFromImage(m_image, options);
        if (!texture) {
            delete node;
            m_uploadedRevision = 0;
            return nullptr;
        }
        // The node owns the texture and deletes the one it replaces.
        node->setTexture(texture);
        m_uploadedRevision = m_revision;
        ++m_uploads;
        const QSGRendererInterface *renderer = window()->rendererInterface();
        m_transfer = renderer && renderer->graphicsApi() == QSGRendererInterface::Software
            ? QStringLiteral("software") : QStringLiteral("gpu");
        // AGENT-GUARD: this runs during sync; notify the GUI thread by a
        // queued call instead of emitting from the render thread.
        QMetaObject::invokeMethod(this, &MediaFrameView::transferChanged, Qt::QueuedConnection);
    }
    node->setRect(target);
    node->setFiltering(smooth() ? QSGTexture::Linear : QSGTexture::Nearest);
    return node;
}

} // namespace QindaTK
