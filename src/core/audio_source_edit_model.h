// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_source_edit.h"

#include <QHash>
#include <QObject>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (D-134, docs/audio-editing.md): the sample edits of every
// source in an AudioSession. The session hands in each decoded original
// (setOriginal); buffer() is the original with the edit list applied, and the
// session plays, renders and analyses that buffer. Every accepted edit is one
// step of this model's own history (per-owner undo, as D-110). A change to a
// source's samples bumps its revision and emits sourceChanged: the session
// drops resampled copies and recompiles, and analysis sources drop their
// peak pyramids and spectral tiles for that source.
// Previews (D-137) run prepare() on a worker thread, can be cancelled at any
// point, and once ready are provisional: buffer() returns the previewed
// samples (heard and analysed) until commitPreview() records the edit or
// cancelPreview() restores the committed samples.
class AudioSourceEditModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AudioSourceEditModel is owned by an AudioSession (session.edits)")
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(QString previewRequestId READ previewRequestId NOTIFY previewChanged)
    Q_PROPERTY(QString previewState READ previewState NOTIFY previewChanged)
    Q_PROPERTY(QString previewResourceId READ previewResourceId NOTIFY previewChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString patchBytes READ patchBytes NOTIFY historyChanged)
    Q_PROPERTY(qint64 patchBudgetBytes READ patchBudgetBytes WRITE setPatchBudgetBytes NOTIFY historyChanged)
public:
    static constexpr int kHistoryLimit = 128;
    // AGENT-NOTE (D-187): spectral patches are a cache of deterministic
    // results. Past this many bytes the oldest are dropped (the newest always
    // stays) and recomputed by prepare() when a fold or redo needs them.
    static constexpr qint64 kDefaultPatchBudgetBytes = qint64(64) * 1024 * 1024;

    explicit AudioSourceEditModel(QObject *parent = nullptr);
    ~AudioSourceEditModel() override;

    // ---- session side ----
    void setOriginal(const QString &resourceId, AudioBufferPtr original);
    void clearSources();
    // The edited samples (or the ready preview), or null when the source has
    // no decoded original. Folds the edit list on first use after a change.
    AudioBufferPtr buffer(const QString &resourceId);
    quint64 revisionNumber(const QString &resourceId) const;

    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const;
    QString redoText() const;
    int historyCount() const { return int(m_history.size()); }
    QString previewRequestId() const { return m_preview.requestId; }
    QString previewState() const { return m_preview.state; }
    QString previewResourceId() const { return m_preview.resourceId; }
    QString lastError() const { return m_lastError; }
    // Bytes held by the prepared spectral patches of committed and redo steps.
    QString patchBytes() const { return QString::number(heldPatchBytes()); }
    qint64 heldPatchBytes() const;
    qint64 patchBudgetBytes() const { return m_patchBudget; }
    void setPatchBudgetBytes(qint64 bytes);

    Q_INVOKABLE QString revision(const QString &resourceId) const { return QString::number(revisionNumber(resourceId)); }
    Q_INVOKABLE QString frames(const QString &resourceId);
    Q_INVOKABLE int channels(const QString &resourceId);
    Q_INVOKABLE int sampleRate(const QString &resourceId);
    Q_INVOKABLE QVariantList edits(const QString &resourceId) const;
    Q_INVOKABLE QVariantList capabilities(const QString &resourceId);
    // Edits that no longer fit their (relinked) original, reported, not played.
    Q_INVOKABLE QStringList problems(const QString &resourceId) const;
    // Synchronous edit, one undo step: {ok, error, revision, frames}.
    Q_INVOKABLE QVariantMap apply(const QString &resourceId, const QVariantMap &edit);
    // Asynchronous preview; a newer preview cancels the older one.
    // Answers with previewReady/previewFailed/previewCanceled(requestId).
    Q_INVOKABLE QVariantMap preview(const QString &requestId, const QString &resourceId, const QVariantMap &edit);
    Q_INVOKABLE QVariantMap commitPreview(const QString &requestId);
    Q_INVOKABLE bool cancelPreview(const QString &requestId);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();

    // Persistence part "edits": {sources: {<resourceId>: [edit maps]}}.
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);

signals:
    void sourceChanged(const QString &resourceId, const QString &revision);
    void historyChanged();
    void previewChanged();
    void previewReady(const QString &requestId, const QVariantMap &summary);
    void previewFailed(const QString &requestId, const QString &error);
    void previewCanceled(const QString &requestId);
    void lastErrorChanged();

private:
    struct Source {
        AudioBufferPtr original;
        QVector<AudioSourceEdit> edits; // committed, in order
        AudioBufferPtr current;         // original + edits; null until folded
        int folded = 0;                 // edits applied to `current`
        QStringList problems;
        quint64 revision = 0;
    };
    struct Preview {
        QString requestId;
        QString resourceId;
        QString state = QStringLiteral("idle"); // idle, computing, ready, failed, canceled
        quint64 baseRevision = 0;
        quint64 serial = 0;
        CancellationSource cancel;
        AudioSourceEdit edit;
        AudioBufferPtr result;
    };
    struct Step {
        QString resourceId;
        AudioSourceEdit edit;
    };
    struct PreviewResult {
        bool ok = false;
        AudioSourceEdit edit;
        AudioBufferPtr result;
        QString error;
    };

    Source *source(const QString &resourceId);
    void fold(Source &source);
    void enforcePatchBudget();
    void touch(const QString &resourceId);
    void push(const QString &resourceId, AudioSourceEdit edit, AudioBufferPtr result);
    void finishPreview(quint64 serial, const PreviewResult &result);
    void dropPreview(const QString &resourceId);
    void setPreviewState(const QString &state);
    QVariantMap fail(const QString &message);

    QHash<QString, Source> m_sources;
    QVector<Step> m_history;
    qsizetype m_cursor = 0;
    Preview m_preview;
    quint64 m_previewSerial = 0;
    quint64 m_revisionSerial = 0;
    quint64 m_editSequence = 0;
    qint64 m_patchBudget = kDefaultPatchBudgetBytes;
    QString m_lastError;
};

} // namespace QindaTK
