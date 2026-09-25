// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_player.h"
#include "resource_resolver.h"
#include "timeline_model.h"

#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/sequence-edits.md "SequenceEditor"): the backend of
// TrimView and TransitionEditor. It turns their requests into TimelineModel
// transactions (the only writer of clip placement, D-045) and publishes
// snapshots in the shapes the views read:
//  - trim: one revision-guarded session per edit point; previewTrim() begins
//    or updates it, so the views show the preview overlay's frames, and
//    commitTrim()/cancelTrim() end it (one history step, or nothing);
//  - transitions: each change is one SetTransition transaction on the
//    incoming clip; previewTransition() loops the bound player over the
//    window, which renders through the same MediaProgram path as export.
// `linked` and `frameExact` are the NLE policies every request carries.
class SequenceEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::TimelineModel *timeline READ timeline WRITE setTimeline NOTIFY bindingChanged)
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY bindingChanged)
    Q_PROPERTY(QindaTK::MediaPlayer *player READ player WRITE setPlayer NOTIFY bindingChanged)
    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY policyChanged)
    Q_PROPERTY(bool frameExact READ frameExact WRITE setFrameExact NOTIFY policyChanged)
    Q_PROPERTY(QString outgoingClipId READ outgoingClipId NOTIFY trimChanged)
    Q_PROPERTY(QString incomingClipId READ incomingClipId NOTIFY trimChanged)
    Q_PROPERTY(QVariantMap trimEdit READ trimEdit NOTIFY trimChanged)
    Q_PROPERTY(QString trimOperationId READ trimOperationId NOTIFY trimChanged)
    Q_PROPERTY(QString trimStatus READ trimStatus NOTIFY trimChanged)
    Q_PROPERTY(QString trimError READ trimError NOTIFY trimChanged)
    Q_PROPERTY(QString trimAppliedDelta READ trimAppliedDelta NOTIFY trimChanged)
    Q_PROPERTY(QString transitionClipId READ transitionClipId WRITE setTransitionClipId NOTIFY transitionChanged)
    Q_PROPERTY(QVariantMap transition READ transition NOTIFY transitionChanged)
    Q_PROPERTY(QString transitionError READ transitionError NOTIFY transitionChanged)
    Q_PROPERTY(QString transitionPreviewState READ transitionPreviewState NOTIFY transitionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit SequenceEditor(QObject *parent = nullptr);
    ~SequenceEditor() override;

    TimelineModel *timeline() const { return m_timeline; }
    void setTimeline(TimelineModel *timeline);
    ResourceResolver *resolver() const { return m_resolver; }
    void setResolver(ResourceResolver *resolver);
    MediaPlayer *player() const { return m_player; }
    void setPlayer(MediaPlayer *player);
    bool linked() const { return m_linked; }
    void setLinked(bool linked);
    bool frameExact() const { return m_frameExact; }
    void setFrameExact(bool exact);
    QString outgoingClipId() const { return m_outgoing; }
    QString incomingClipId() const { return m_incoming; }
    QVariantMap trimEdit() const { return m_trimEdit; }
    QString trimOperationId() const { return m_trimOperation; }
    QString trimStatus() const { return m_trimStatus; }
    QString trimError() const { return m_trimError; }
    QString trimAppliedDelta() const { return m_trimApplied; }
    QString transitionClipId() const { return m_transitionClip; }
    void setTransitionClipId(const QString &clipId);
    QVariantMap transition() const { return m_transition; }
    QString transitionError() const { return m_transitionError; }
    QString transitionPreviewState() const { return m_transitionPreview; }
    QString lastError() const { return m_lastError; }

    // Selects the cut between two adjacent clips on one track.
    Q_INVOKABLE bool selectEditPoint(const QString &outgoingClipId, const QString &incomingClipId);
    // Selects the cut on `trackId` nearest `ticks` (a clip start another clip ends at).
    Q_INVOKABLE bool selectCut(const QString &trackId, const QString &ticks);
    // mode: roll, ripple, slip or slide; side: "left" (outgoing clip) or
    // "right" (incoming clip) for ripple, slip and slide. Returns {ok,
    // operationId, status, appliedDelta, error}.
    Q_INVOKABLE QVariantMap previewTrim(const QString &mode, const QString &side, const QString &deltaTicks);
    Q_INVOKABLE QVariantMap commitTrim();
    Q_INVOKABLE bool cancelTrim();
    // transition: {type, durationTicks, alignment, parameters}; one transaction.
    Q_INVOKABLE QVariantMap setTransition(const QString &clipId, const QVariantMap &transition);
    Q_INVOKABLE QVariantMap removeTransition(const QString &clipId);
    // TransitionEditor requests on the selected clip: "type", "durationTicks",
    // "alignment" or "parameter:<id>".
    Q_INVOKABLE QVariantMap editTransition(const QString &parameter, const QString &value);
    // {ok, startTicks, endTicks} of the clip's transition window; ok false without one.
    Q_INVOKABLE QVariantMap transitionWindow(const QString &clipId) const;
    Q_INVOKABLE QVariantMap previewTransition();
    Q_INVOKABLE bool stopTransitionPreview();
    Q_INVOKABLE QVariantMap linkClips(const QStringList &clipIds);
    Q_INVOKABLE QVariantMap unlinkClips(const QStringList &clipIds);

signals:
    void bindingChanged();
    void policyChanged();
    void trimChanged();
    void transitionChanged();
    void lastErrorChanged();

private:
    bool isMissing(const QString &resourceId) const;
    QVariantMap apply(TimelineEditRequest request, qint64 pointer);
    void refreshTrim();
    void refreshTransition();
    void resetTrimSession(const QString &status, const QString &error);
    void setError(const QString &error);
    QString handleText(qint64 ticks) const;

    QPointer<TimelineModel> m_timeline;
    QPointer<ResourceResolver> m_resolver;
    QPointer<MediaPlayer> m_player;
    QList<QMetaObject::Connection> m_links;
    bool m_linked = true;
    bool m_frameExact = true;
    QString m_outgoing;
    QString m_incoming;
    QVariantMap m_trimEdit;
    QString m_trimOperation;
    QString m_trimKey;
    QString m_trimStatus = QStringLiteral("idle");
    QString m_trimError;
    QString m_trimApplied = QStringLiteral("0");
    QString m_transitionClip;
    QVariantMap m_transition;
    QString m_transitionError;
    QString m_transitionPreview = QStringLiteral("idle");
    QString m_previewFirstSample;
    QString m_previewLastSample;
    QString m_lastError;
};

} // namespace QindaTK
