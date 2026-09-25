// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "video_scope_analysis.h"

#include <QImage>
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// AGENT-GUARD: media_player.h reaches color_grade_edit_session.h, which
// includes this header; including media_player.h here makes a cycle.
namespace QindaTK { class MediaPlayer; }
Q_MOC_INCLUDE("media_player.h")

// AGENT-CONTRACT (docs/video-scopes.md, D-127): the scope snapshot owner.
// A snapshot is computed on demand -- analyzeFrame(), a convention change,
// or a newly presented `player` frame while `active` -- never per repaint;
// VideoScopes only paints the immutable snapshot. Bound to a MediaPlayer,
// each snapshot's frameId is the player's presentedFrameKey for the exact
// frame measured (after the program's finishing), so scrubbing never shows
// one frame's scope under another frame's identity.
class VideoScopesModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString matrix READ matrix WRITE setMatrix NOTIFY changed)
    Q_PROPERTY(QString signalRange READ signalRange WRITE setSignalRange NOTIFY changed)
    Q_PROPERTY(QString analysisSpace READ analysisSpace WRITE setAnalysisSpace NOTIFY changed)
    Q_PROPERTY(QindaTK::MediaPlayer *player READ player WRITE setPlayer NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(int analysisCount READ analysisCount NOTIFY changed)

public:
    explicit VideoScopesModel(QObject *parent = nullptr);

    QVariantMap snapshot() const { return m_snapshot; }
    QString error() const { return m_error; }
    QString matrix() const { return VideoScopeConvention::matrixName(m_convention.matrix); }
    void setMatrix(const QString &name);
    QString signalRange() const { return VideoScopeConvention::rangeName(m_convention.range); }
    void setSignalRange(const QString &name);
    QString analysisSpace() const { return VideoScopeConvention::spaceName(m_convention.space); }
    void setAnalysisSpace(const QString &name);
    QindaTK::MediaPlayer *player() const { return m_player; }
    void setPlayer(QindaTK::MediaPlayer *player);
    bool active() const { return m_active; }
    void setActive(bool active);
    int analysisCount() const { return m_analysisCount; }
    const VideoScopeConvention &convention() const { return m_convention; }

    Q_INVOKABLE QVariantMap analyzeFrame(const QImage &frame, const QString &frameId);
    Q_INVOKABLE QVariantMap analyzeColorBarsFixture(const QString &frameId = QStringLiteral("synthetic-color-bars"));
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    QVariantMap failed(const QString &message);
    void conventionChanged();
    void playerFrameChanged();

    VideoScopeConvention m_convention;
    QVariantMap m_snapshot;
    QString m_error;
    QImage m_frame; // the last analyzed frame, re-measured when the convention changes
    QString m_frameId;
    QindaTK::MediaPlayer *m_player = nullptr; // cleared by its destroyed() signal
    QMetaObject::Connection m_playerLink;
    QMetaObject::Connection m_playerGone;
    quint64 m_playerRevision = 0;
    bool m_active = true;
    bool m_stale = false;
    int m_analysisCount = 0;
};
