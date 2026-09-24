// SPDX-License-Identifier: LGPL-3.0-or-later
#include "video_finishing.h"

namespace QindaTK {

VideoFinishing::VideoFinishing(QObject *parent) : QObject(parent) {}

void VideoFinishing::setGradeSession(ColorGradeEditSession *session)
{
    if (m_session == session) return;
    disconnect(m_sessionLink);
    m_session = session;
    if (session)
        m_sessionLink = connect(session, &ColorGradeEditSession::changed, this, &VideoFinishing::sourceChanged);
    sourceChanged();
    emit changed();
}

void VideoFinishing::setCaptionTrack(CaptionTrackModel *track)
{
    if (m_track == track) return;
    disconnect(m_trackLink);
    m_track = track;
    if (track) m_trackLink = connect(track, &CaptionTrackModel::changed, this, &VideoFinishing::sourceChanged);
    sourceChanged();
    emit changed();
}

void VideoFinishing::setGradeEnabled(bool enabled)
{
    if (m_gradeEnabled == enabled) return;
    m_gradeEnabled = enabled;
    sourceChanged();
    emit changed();
}

void VideoFinishing::setBurnInCaptions(bool enabled)
{
    if (m_burnIn == enabled) return;
    m_burnIn = enabled;
    sourceChanged();
    emit changed();
}

void VideoFinishing::setEmbedCaptions(bool enabled)
{
    if (m_embed == enabled) return;
    m_embed = enabled;
    sourceChanged();
    emit changed();
}

MediaFinishing VideoFinishing::value(bool includePreview) const
{
    MediaFinishing finishing;
    if (m_session && m_gradeEnabled) {
        const QVariantMap parameters = includePreview ? m_session->parameters() : m_session->committedParameters();
        finishing.grade = ColorGradeParameters::fromVariantMap(parameters);
    }
    if (m_track) {
        if (m_burnIn) finishing.burnedCaptions = m_track->cues();
        if (m_embed) {
            finishing.embeddedCaptions = m_track->cues();
            finishing.embeddedLanguage = m_track->language();
        }
    }
    return finishing;
}

QString VideoFinishing::summary() const
{
    const MediaFinishing finishing = value(true);
    QStringList parts;
    MediaFinishing gradeOnly;
    gradeOnly.grade = finishing.grade;
    if (gradeOnly.changesPictures()) parts << tr("graded");
    if (!finishing.burnedCaptions.isEmpty()) parts << tr("%n burned-in caption(s)", nullptr, int(finishing.burnedCaptions.size()));
    if (!finishing.embeddedCaptions.isEmpty())
        parts << tr("%n embedded caption(s) (%1)", nullptr, int(finishing.embeddedCaptions.size()))
                     .arg(finishing.embeddedLanguage);
    return parts.isEmpty() ? tr("No finishing") : parts.join(QStringLiteral(" · "));
}

void VideoFinishing::sourceChanged()
{
    const QString next = value(true).digest();
    if (next == m_digest) return;
    m_digest = next;
    emit changed();
}

} // namespace QindaTK
