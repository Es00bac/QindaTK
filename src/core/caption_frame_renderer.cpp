// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caption_frame_renderer.h"

#include "caption_track_codec.h"
#include "caption_track_model.h"

#include <QBuffer>
#include <QColorSpace>
#include <QDir>
#include <QJsonDocument>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <algorithm>

namespace {
struct ActiveCaption {
    CaptionCueRecord cue;
};

bool parseMilliseconds(const QString &text, qint64 *milliseconds)
{
    if (text.isEmpty()) return false;
    for (const QChar ch : text) if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return false;
    bool ok = false;
    const qint64 value = text.toLongLong(&ok, 10);
    if (!ok || value < 0) return false;
    *milliseconds = value;
    return true;
}

QString drawText(QPainter &painter, const ActiveCaption &active, const QRectF &bounds,
                 int fontSize, qreal outlineWidth)
{
    const bool emphasis = active.cue.styleId == QLatin1String("emphasis");
    QFont font(QStringLiteral("Sans Serif"));
    font.setPixelSize(fontSize);
    font.setWeight(emphasis ? QFont::DemiBold : QFont::Medium);
    painter.setFont(font);
    const QColor fill = emphasis ? QColor(255, 225, 130) : QColor(255, 255, 255);
    const QRectF textBounds = bounds.adjusted(4, 2, -4, -2);
    constexpr int flags = Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap;
    const qreal offset = qMax<qreal>(1.0, outlineWidth * 0.65);
    painter.setPen(QColor(0, 0, 0, 230));
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            painter.drawText(textBounds.translated(dx * offset, dy * offset), flags, active.cue.text);
        }
    }
    painter.setPen(fill);
    painter.drawText(textBounds, flags, active.cue.text);
    return active.cue.text;
}
}

QStringList CaptionBurnIn::burn(QImage *image, const QVector<CaptionCueRecord> &cues, qint64 timeMs)
{
    QVector<ActiveCaption> active;
    for (const auto &cue : cues)
        if (cue.inMs <= timeMs && timeMs < cue.outMs) active.append({cue});
    if (!image || image->isNull() || active.isEmpty()) return {};
    std::sort(active.begin(), active.end(), [](const ActiveCaption &a, const ActiveCaption &b) {
        if (a.cue.lane != b.cue.lane) return a.cue.lane < b.cue.lane;
        if (a.cue.inMs != b.cue.inMs) return a.cue.inMs < b.cue.inMs;
        return a.cue.id < b.cue.id;
    });
    QPainter painter(image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const qreal width = image->width();
    const qreal height = image->height();
    const qreal top = height * 0.22;
    const qreal bandHeight = height * 0.70;
    const qreal slotHeight = bandHeight / active.size();
    const qreal desiredFont = qMin(height * 0.05, slotHeight * 0.58);
    const int fontSize = qMax(8, qMin(128, int(desiredFont)));
    const qreal outline = qMax(1.0, fontSize * 0.09);
    QStringList drawn;
    for (qsizetype i = 0; i < active.size(); ++i) {
        const QRectF slot(width * 0.055, top + i * slotHeight, width * 0.89, slotHeight);
        drawText(painter, active.at(i), slot, fontSize, outline);
        drawn.append(active.at(i).cue.id);
    }
    painter.end();
    return drawn;
}

CaptionFrameRenderer::CaptionFrameRenderer(QObject *parent) : QObject(parent) {}

QString CaptionFrameRenderer::defaultOutputPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/caption-frame.png");
}

QString CaptionFrameRenderer::pngDataUrl(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QImage CaptionFrameRenderer::fixtureFrame()
{
    QImage image(960, 540, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, image.width(), image.height());
    gradient.setColorAt(0, QColor(23, 55, 76));
    gradient.setColorAt(0.55, QColor(38, 96, 115));
    gradient.setColorAt(1, QColor(19, 37, 57));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(54, 125, 112));
    painter.drawEllipse(QRectF(105, 120, 360, 350));
    painter.setBrush(QColor(182, 131, 79));
    painter.drawRoundedRect(QRectF(545, 145, 275, 255), 24, 24);
    painter.setPen(QPen(QColor(225, 240, 242, 120), 3));
    painter.drawLine(QPointF(0, 475), QPointF(image.width(), 475));
    painter.end();
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

QVariantMap CaptionFrameRenderer::renderFrame(const QImage &source, CaptionTrackModel *track,
                                               const QString &timeMilliseconds)
{
    qint64 timeMs = 0;
    if (source.isNull() || !track || !parseMilliseconds(timeMilliseconds, &timeMs)) {
        m_error = QStringLiteral("Rendering needs a decoded frame, caption track and non-negative decimal millisecond string.");
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    QVector<CaptionCueRecord> cues;
    QString trackId, language, parseError;
    const auto document = QJsonDocument::fromJson(track->toJson().toUtf8());
    if (!document.isObject() || !CaptionTrackCodec::fromJson(document.object(), &cues,
                                                               &trackId, &language, &parseError)) {
        m_error = parseError.isEmpty() ? QStringLiteral("Caption track JSON is unavailable.") : parseError;
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    QVector<ActiveCaption> active;
    QSet<QString> supportedStyles;
    for (const QVariant &value : track->styles()) {
        const QVariantMap style = value.toMap();
        if (style.value(QStringLiteral("supported")).toBool())
            supportedStyles.insert(style.value(QStringLiteral("id")).toString());
    }
    for (const auto &cue : cues) {
        if (cue.inMs > timeMs || timeMs >= cue.outMs) continue;
        if (!supportedStyles.contains(cue.styleId)) {
            m_error = QStringLiteral("Active caption cue %1 uses unsupported style %2.").arg(cue.id, cue.styleId);
            emit changed();
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
        }
        active.append({cue});
    }
    m_rendered = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QVector<CaptionCueRecord> activeCues;
    for (const ActiveCaption &caption : std::as_const(active)) activeCues.append(caption.cue);
    m_activeCueIds = CaptionBurnIn::burn(&m_rendered, activeCues, timeMs);
    m_imageDataUrl = pngDataUrl(m_rendered);
    m_lastTimeMs = timeMilliseconds;
    m_error = m_imageDataUrl.isEmpty() ? QStringLiteral("Could not encode rendered frame as PNG.") : QString();
    emit changed();
    return {{QStringLiteral("ok"), m_error.isEmpty()}, {QStringLiteral("timeMs"), m_lastTimeMs},
            {QStringLiteral("activeCueIds"), m_activeCueIds}, {QStringLiteral("imageDataUrl"), m_imageDataUrl},
            {QStringLiteral("error"), m_error}};
}

QVariantMap CaptionFrameRenderer::renderFixture(CaptionTrackModel *track, const QString &timeMilliseconds)
{
    return renderFrame(fixtureFrame(), track, timeMilliseconds);
}

bool CaptionFrameRenderer::saveRenderedFrame(const QString &path)
{
    const QString outputPath = path.trimmed().isEmpty() ? defaultOutputPath() : path;
    if (outputPath.trimmed().isEmpty() || m_rendered.isNull()) {
        m_error = QStringLiteral("No rendered frame or output path is available.");
        emit changed();
        return false;
    }
    if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
        m_error = QStringLiteral("Could not create rendered-frame output directory.");
        emit changed();
        return false;
    }
    QSaveFile file(outputPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !m_rendered.save(&file, "PNG") || !file.commit()) {
        m_error = file.errorString();
        emit changed();
        return false;
    }
    m_error.clear();
    emit changed();
    return true;
}
