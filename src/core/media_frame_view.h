// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_player.h"

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/media-playback.md "Frame transfer"): displays the
// player's presented frame, letterboxed at the source's display aspect. The
// item keeps one implicitly shared copy of that frame; the scene graph node
// owns the texture created from it (QSGImageNode::setOwnsTexture) and
// replaces it only when a new frame is presented. On the RHI adaptation the
// upload is a GPU texture; on the software adaptation the node keeps the
// image. displayedFrame always names the frame whose pixels are on screen.
class MediaFrameView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::MediaPlayer *player READ player WRITE setPlayer NOTIFY playerChanged)
    Q_PROPERTY(QString displayedFrame READ displayedFrame NOTIFY displayedChanged)
    Q_PROPERTY(QString displayedFrameKey READ displayedFrameKey NOTIFY displayedChanged)
    Q_PROPERTY(QRectF frameRect READ frameRect NOTIFY displayedChanged)
    Q_PROPERTY(QString frameTransfer READ frameTransfer NOTIFY transferChanged)
    Q_PROPERTY(int textureUploads READ textureUploads NOTIFY transferChanged)
public:
    explicit MediaFrameView(QQuickItem *parent = nullptr);
    MediaPlayer *player() const { return m_player; }
    void setPlayer(MediaPlayer *player);
    QString displayedFrame() const { return m_frame >= 0 ? QString::number(m_frame) : QString(); }
    QString displayedFrameKey() const { return m_key; }
    QRectF frameRect() const;
    // "none" until the first upload, then "gpu" or "software".
    QString frameTransfer() const { return m_transfer; }
    int textureUploads() const { return m_uploads; }

signals:
    void playerChanged();
    void displayedChanged();
    void transferChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void takeFrame();
    QPointer<MediaPlayer> m_player;
    QMetaObject::Connection m_link;
    QImage m_image; // premultiplied when the frame carries alpha
    qint64 m_frame = -1;
    QString m_key;
    double m_pixelAspect = 1.0;
    quint64 m_playerRevision = 0;
    quint64 m_revision = 0;         // GUI thread: frames taken from the player
    quint64 m_uploadedRevision = 0; // written during scene graph sync only
    int m_uploads = 0;
    QString m_transfer = QStringLiteral("none");
};

} // namespace QindaTK
