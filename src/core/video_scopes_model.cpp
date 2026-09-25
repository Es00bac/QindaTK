// SPDX-License-Identifier: LGPL-3.0-or-later
#include "video_scopes_model.h"
#include "media_player.h"

#include <QColorSpace>

VideoScopesModel::VideoScopesModel(QObject *parent) : QObject(parent) {}

QVariantMap VideoScopesModel::failed(const QString &message)
{
    m_error = message;
    m_snapshot = {{QStringLiteral("valid"), false}, {QStringLiteral("error"), message}};
    emit changed();
    return m_snapshot;
}

void VideoScopesModel::clear()
{
    m_error.clear();
    m_snapshot.clear();
    m_frame = {};
    m_frameId.clear();
    emit changed();
}

void VideoScopesModel::setMatrix(const QString &name)
{
    const auto matrix = VideoScopeConvention::matrixFrom(name);
    if (!matrix) { failed(QStringLiteral("Unknown scope matrix %1 (bt709, bt601, bt2020).").arg(name)); return; }
    if (*matrix == m_convention.matrix) return;
    m_convention.matrix = *matrix;
    conventionChanged();
}

void VideoScopesModel::setSignalRange(const QString &name)
{
    const auto range = VideoScopeConvention::rangeFrom(name);
    if (!range) { failed(QStringLiteral("Unknown signal range %1 (full, limited).").arg(name)); return; }
    if (*range == m_convention.range) return;
    m_convention.range = *range;
    conventionChanged();
}

void VideoScopesModel::setAnalysisSpace(const QString &name)
{
    const auto space = VideoScopeConvention::spaceFrom(name);
    if (!space) { failed(QStringLiteral("Unknown analysis space %1 (srgb, signal).").arg(name)); return; }
    if (*space == m_convention.space) return;
    m_convention.space = *space;
    conventionChanged();
}

void VideoScopesModel::conventionChanged()
{
    if (!m_frame.isNull() && m_active) analyzeFrame(m_frame, m_frameId);
    else emit changed();
}

void VideoScopesModel::setPlayer(QindaTK::MediaPlayer *player)
{
    if (m_player == player) return;
    disconnect(m_playerLink);
    disconnect(m_playerGone);
    m_player = player;
    m_playerRevision = 0;
    if (player) {
        m_playerLink = connect(player, &QindaTK::MediaPlayer::frameChanged, this, &VideoScopesModel::playerFrameChanged);
        m_playerGone = connect(player, &QObject::destroyed, this, [this] {
            m_player = nullptr;
            emit changed();
        });
    }
    emit changed();
    playerFrameChanged();
}

void VideoScopesModel::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    emit changed();
    if (active && m_stale) playerFrameChanged();
}

void VideoScopesModel::playerFrameChanged()
{
    if (!m_player) return;
    const QImage image = m_player->presentedImage();
    const quint64 revision = m_player->presentedRevision();
    if (image.isNull() || revision == m_playerRevision) return;
    if (!m_active) {
        m_stale = true;
        return;
    }
    m_stale = false;
    m_playerRevision = revision;
    analyzeFrame(image, m_player->presentedFrameKey());
}

QVariantMap VideoScopesModel::analyzeColorBarsFixture(const QString &frameId)
{
    // Eight equal-width SMPTE-like encoded sRGB bars exercise primary and
    // secondary chroma without implying a broadcast legal-range signal.
    const QRgb bars[] = {qRgb(255, 255, 255), qRgb(255, 255, 0), qRgb(0, 255, 255),
                         qRgb(0, 255, 0), qRgb(255, 0, 255), qRgb(255, 0, 0),
                         qRgb(0, 0, 255), qRgb(0, 0, 0)};
    QImage image(256, 64, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = bars[qMin(7, x * 8 / image.width())];
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return analyzeFrame(image, frameId);
}

QVariantMap VideoScopesModel::analyzeFrame(const QImage &frame, const QString &frameId)
{
    const QVariantMap result = VideoScopeAnalysis::analyze(frame, frameId, m_convention);
    if (!result.value(QStringLiteral("valid")).toBool())
        return failed(result.value(QStringLiteral("error")).toString());
    m_frame = frame;
    m_frameId = frameId;
    m_error.clear();
    m_snapshot = result;
    ++m_analysisCount;
    emit changed();
    return m_snapshot;
}
