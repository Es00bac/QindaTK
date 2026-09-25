// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "caption_track_model.h"
#include "color_grade_edit_session.h"
#include "media_finishing.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/media-playback.md "Finishing", D-126): gathers the
// grade of a ColorGradeEditSession and the cues of a CaptionTrackModel into
// the MediaFinishing that MediaPlayer attaches to its program. The monitor
// shows value(true), which includes an open grade gesture's preview; exports
// use value(false), the committed grade, so a drag in progress never reaches
// a file. `changed` fires once per setter call, and on a source change only
// when the finishing's digest changes. An attached session is switched to
// ColorGradeEditSession::ProgramGrade: frames the host feeds it replace the
// preview source and never reset the program grade.
class VideoFinishing : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::ColorGradeEditSession *gradeSession READ gradeSession WRITE setGradeSession NOTIFY changed)
    Q_PROPERTY(CaptionTrackModel *captionTrack READ captionTrack WRITE setCaptionTrack NOTIFY changed)
    Q_PROPERTY(bool gradeEnabled READ gradeEnabled WRITE setGradeEnabled NOTIFY changed)
    Q_PROPERTY(bool burnInCaptions READ burnInCaptions WRITE setBurnInCaptions NOTIFY changed)
    Q_PROPERTY(bool embedCaptions READ embedCaptions WRITE setEmbedCaptions NOTIFY changed)
    Q_PROPERTY(QString digest READ digest NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
public:
    explicit VideoFinishing(QObject *parent = nullptr);

    ColorGradeEditSession *gradeSession() const { return m_session; }
    void setGradeSession(ColorGradeEditSession *session);
    CaptionTrackModel *captionTrack() const { return m_track; }
    void setCaptionTrack(CaptionTrackModel *track);
    bool gradeEnabled() const { return m_gradeEnabled; }
    void setGradeEnabled(bool enabled);
    bool burnInCaptions() const { return m_burnIn; }
    void setBurnInCaptions(bool enabled);
    bool embedCaptions() const { return m_embed; }
    void setEmbedCaptions(bool enabled);
    QString digest() const { return m_digest; }
    QString summary() const;

    MediaFinishing value(bool includePreview) const;

signals:
    void changed();

private:
    bool sourceChanged(); // emits changed() and returns true when the digest moved

    QPointer<ColorGradeEditSession> m_session;
    QPointer<CaptionTrackModel> m_track;
    QMetaObject::Connection m_sessionLink;
    QMetaObject::Connection m_trackLink;
    bool m_gradeEnabled = true;
    bool m_burnIn = false;
    bool m_embed = false;
    QString m_digest;
};

} // namespace QindaTK
