// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_media.h"
#include "audio_take_comp.h"

#include <QHash>
#include <QObject>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace QindaTK {

// AGENT-CONTRACT (D-174..D-177, docs/audio-comping.md): the take comps of an
// AudioSession (session.comps). A timeline clip whose resourceId is a comp ID
// plays that comp through AudioRenderGraph; this model owns what the comp
// selects. Every accepted edit is one step of the model's own history
// (per-owner undo, as D-110 and D-134): a step stores the whole comp before
// and after. A preview is provisional: playback() returns it (heard in the
// engine and in renders) until commitPreview() records it or cancelPreview()
// restores the committed comp. Audition and comparison choose what plays
// without changing the comp; they are neither saved nor undone.
class AudioTakeCompModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AudioTakeCompModel is owned by an AudioSession (session.comps)")
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QStringList compIds READ compIds NOTIFY revisionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(QString previewCompId READ previewCompId NOTIFY revisionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kHistoryLimit = 128;
    // Decoded source samples at their own rate, or null when unavailable.
    using SourceLookup = std::function<AudioBufferPtr(const QString &resourceId)>;

    explicit AudioTakeCompModel(QObject *parent = nullptr);

    // ---- session side ----
    void setSourceLookup(SourceLookup lookup) { m_sources = std::move(lookup); }
    const AudioTakeComp *comp(const QString &compId) const;
    // What plays now: the preview of that comp when one is active, else the
    // committed comp, resolved with its audition take.
    AudioCompPlayback playback(const QString &compId) const;
    // A new comp (a recording or a converted clip). Not a history step: the
    // timeline step that places its clip is the undoable part.
    bool insert(const AudioTakeComp &comp, QString *error = nullptr);
    // Removes a comp no clip references (a placement that failed).
    bool remove(const QString &compId);
    // Recording into an existing comp: adds `takes` and selects each range
    // (in order, the later wins) as one history step.
    struct Selection { QString takeId; qint64 start = 0; qint64 end = 0; };
    bool record(const QString &compId, const QVector<AudioTake> &takes, const QVector<Selection> &selections,
                QString *error = nullptr);
    // Media availability changed (open, relink, edits): views re-read.
    void sourcesChanged();

    int revision() const { return m_revision; }
    QStringList compIds() const;
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const;
    QString redoText() const;
    int historyCount() const { return int(m_history.size()); }
    QString previewCompId() const { return m_preview.active ? m_preview.comp.id : QString(); }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QVariantMap compData(const QString &compId) const;
    // The resolved pieces that play (segments with their crossfades), as
    // exact decimal sample strings.
    Q_INVOKABLE QVariantList pieces(const QString &compId) const;
    // TakeCompView snapshot: {takes, compSegments, sampleBoundaries} for a
    // comp; boundaries are `columns` equal cells plus every segment and fade
    // edge, so each shown edge is an exact swipe boundary. Takes carry
    // `peaks` (max |sample| of `peakCount` columns) and `available`.
    Q_INVOKABLE QVariantMap viewState(const QString &compId, int columns = 256, int peakCount = 64) const;
    // Edits, one undo step each: {ok, error, changed}. Samples are decimal strings.
    Q_INVOKABLE QVariantMap select(const QString &compId, const QString &takeId, const QString &startSample,
                                   const QString &endSample);
    Q_INVOKABLE QVariantMap split(const QString &compId, const QString &atSample);
    Q_INVOKABLE QVariantMap merge(const QString &compId, const QString &segmentId);
    Q_INVOKABLE QVariantMap clear(const QString &compId, const QString &startSample, const QString &endSample);
    Q_INVOKABLE QVariantMap setFade(const QString &compId, const QString &samples, const QString &shape);
    // Provisional selection (a swipe in progress): heard until committed or
    // cancelled. A newer preview replaces the older one; any other edit,
    // undo or redo cancels it first.
    Q_INVOKABLE QVariantMap preview(const QString &compId, const QString &takeId, const QString &startSample,
                                    const QString &endSample);
    Q_INVOKABLE QVariantMap commitPreview();
    Q_INVOKABLE bool cancelPreview();
    // Plays one take alone over the comp ("" plays the comp again).
    Q_INVOKABLE bool setAudition(const QString &compId, const QString &takeId);
    Q_INVOKABLE QString audition(const QString &compId) const;
    // A/B comparison: auditions `takeId`; toggleComparison switches between it
    // and `referenceTakeId` and returns the take now heard.
    Q_INVOKABLE bool compare(const QString &compId, const QString &takeId, const QString &referenceTakeId);
    Q_INVOKABLE QString toggleComparison(const QString &compId);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();

    // Persistence part "comps": {comps: [comp maps]}. All or nothing.
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);

signals:
    // Anything that changes what plays: content, preview, audition. The
    // session recompiles its graph.
    void changed();
    // Anything a view shows changed (also source availability): re-read
    // viewState(). Every changed() comes with one.
    void revisionChanged();
    void historyChanged();
    void lastErrorChanged();

private:
    struct Step {
        QString label;
        AudioTakeComp before;
        AudioTakeComp after;
    };
    struct Preview {
        bool active = false;
        AudioTakeComp comp;
    };
    struct Listening {
        QString take;
        QString a;
        QString b;
    };
    using Edit = std::function<QString(AudioTakeComp &)>;

    QVariantMap edit(const QString &compId, const QString &label, const Edit &change);
    void push(const QString &label, const AudioTakeComp &before, const AudioTakeComp &after);
    void restore(const AudioTakeComp &comp);
    void dropPreview();
    void touch();
    QVariantMap fail(const QString &message);
    void clearError();

    QHash<QString, AudioTakeComp> m_comps;
    QStringList m_order;
    QHash<QString, Listening> m_listening;
    QVector<Step> m_history;
    qsizetype m_cursor = 0;
    Preview m_preview;
    SourceLookup m_sources;
    int m_revision = 0;
    QString m_lastError;
};

} // namespace QindaTK
