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
    if (name == QLatin1String("source")) {
        if (m_sourceMatrix) return;
        m_sourceMatrix = true;
        m_convention.matrix = VideoScopeConvention::Matrix::Bt709;
        conventionChanged();
        return;
    }
    const auto matrix = VideoScopeConvention::matrixFrom(name);
    if (!matrix) { failed(QStringLiteral("Unknown scope matrix %1 (source, bt709, bt601, bt2020).").arg(name)); return; }
    if (!m_sourceMatrix && *matrix == m_convention.matrix) return;
    m_sourceMatrix = false;
    m_convention.matrix = *matrix;
    conventionChanged();
}

void VideoScopesModel::setSignalRange(const QString &name)
{
    if (name == QLatin1String("source")) {
        if (m_sourceRange) return;
        m_sourceRange = true;
        m_convention.range = VideoScopeConvention::Range::Full;
        conventionChanged();
        return;
    }
    const auto range = VideoScopeConvention::rangeFrom(name);
    if (!range) { failed(QStringLiteral("Unknown signal range %1 (source, full, limited).").arg(name)); return; }
    if (!m_sourceRange && *range == m_convention.range) return;
    m_sourceRange = false;
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
    if (!m_frame.isNull() && m_active) analyzeDecodedFrame(m_frame, m_frameId, m_frameMatrix, m_frameRange);
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
    // Every drawn source must agree on its decode; a mix has no one coding.
    QString matrix, range;
    bool first = true;
    for (const QVariant &value : m_player->presentedSources()) {
        const QVariantMap source = value.toMap();
        const QString m = source.value(QStringLiteral("matrix")).toString();
        const QString r = source.value(QStringLiteral("range")).toString();
        if (first) { matrix = m; range = r; first = false; }
        else if (m != matrix || r != range) { matrix = QStringLiteral("mixed"); range.clear(); }
    }
    analyzeDecodedFrame(image, m_player->presentedFrameKey(), matrix, range);
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
    return analyzeDecodedFrame(frame, frameId, {}, {});
}

QVariantMap VideoScopesModel::analyzeDecodedFrame(const QImage &frame, const QString &frameId,
                                                  const QString &sourceMatrix, const QString &sourceRange)
{
    VideoScopeConvention convention = m_convention;
    QString matrixOrigin = QStringLiteral("stated");
    QString rangeOrigin = QStringLiteral("stated");
    if (m_sourceMatrix) {
        // "rgb" (no matrix), "mixed", smpte240m/fcc and unknown fall back.
        const auto matrix = VideoScopeConvention::matrixFrom(sourceMatrix);
        convention.matrix = matrix.value_or(VideoScopeConvention::Matrix::Bt709);
        matrixOrigin = matrix ? QStringLiteral("source") : QStringLiteral("default");
    }
    if (m_sourceRange) {
        const auto range = VideoScopeConvention::rangeFrom(sourceRange);
        convention.range = range.value_or(VideoScopeConvention::Range::Full);
        rangeOrigin = range ? QStringLiteral("source") : QStringLiteral("default");
    }
    QVariantMap result = VideoScopeAnalysis::analyze(frame, frameId, convention);
    if (!result.value(QStringLiteral("valid")).toBool())
        return failed(result.value(QStringLiteral("error")).toString());
    result.insert(QStringLiteral("matrixOrigin"), matrixOrigin);
    result.insert(QStringLiteral("rangeOrigin"), rangeOrigin);
    result.insert(QStringLiteral("sourceCoding"), sourceMatrix.isEmpty() ? QString()
                  : sourceRange.isEmpty() ? sourceMatrix : sourceMatrix + QLatin1Char('/') + sourceRange);
    m_frame = frame;
    m_frameId = frameId;
    m_frameMatrix = sourceMatrix;
    m_frameRange = sourceRange;
    m_error.clear();
    m_snapshot = result;
    ++m_analysisCount;
    emit changed();
    return m_snapshot;
}
