// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_source_edit_model.h"

#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace QindaTK {
namespace {

QVariantMap success(quint64 revision, const AudioBufferPtr &buffer)
{
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("revision"), QString::number(revision)},
            {QStringLiteral("frames"), QString::number(buffer ? buffer->frames : 0)}};
}

bool active(const QString &state)
{
    return state == QLatin1String("computing") || state == QLatin1String("ready");
}

} // namespace

AudioSourceEditModel::AudioSourceEditModel(QObject *parent)
    : QObject(parent)
{
}

AudioSourceEditModel::~AudioSourceEditModel()
{
    // A running preview stops at its next frame; its watcher dies with us.
    m_preview.cancel.cancel();
}

AudioSourceEditModel::Source *AudioSourceEditModel::source(const QString &resourceId)
{
    const auto it = m_sources.find(resourceId);
    return it == m_sources.end() ? nullptr : &it.value();
}

quint64 AudioSourceEditModel::revisionNumber(const QString &resourceId) const
{
    return m_sources.value(resourceId).revision;
}

QString AudioSourceEditModel::undoText() const
{
    return canUndo() ? m_history.at(m_cursor - 1).edit.label() : QString();
}

QString AudioSourceEditModel::redoText() const
{
    return canRedo() ? m_history.at(m_cursor).edit.label() : QString();
}

void AudioSourceEditModel::touch(const QString &resourceId)
{
    Source &entry = m_sources[resourceId];
    entry.revision = ++m_revisionSerial;
    emit sourceChanged(resourceId, QString::number(entry.revision));
}

void AudioSourceEditModel::setOriginal(const QString &resourceId, AudioBufferPtr original)
{
    Source *existing = source(resourceId);
    if (existing ? existing->original == original : !original) return;
    dropPreview(resourceId);
    Source &entry = m_sources[resourceId];
    entry.original = std::move(original);
    entry.current.reset();
    entry.folded = 0;
    entry.problems.clear();
    // AGENT-GUARD: prepared patches hold samples computed from the previous
    // original; a relinked file must recompute them (fold/redo prepare again).
    for (AudioSourceEdit &edit : entry.edits) edit.patch.reset();
    for (Step &step : m_history)
        if (step.resourceId == resourceId) step.edit.patch.reset();
    touch(resourceId);
}

void AudioSourceEditModel::clearSources()
{
    dropPreview({});
    const QStringList ids = m_sources.keys();
    m_sources.clear();
    clearHistory();
    for (const QString &id : ids) touch(id);
}

void AudioSourceEditModel::fold(Source &entry)
{
    if (!entry.original) return;
    if (!entry.current) {
        entry.current = entry.original;
        entry.folded = 0;
        entry.problems.clear();
    }
    for (; entry.folded < entry.edits.size(); ++entry.folded) {
        AudioSourceEdit &edit = entry.edits[entry.folded];
        QString problem;
        if (!edit.patch && !edit.changesLength()) AudioSourceEditing::prepare(*entry.current, edit, {}, &problem);
        else problem = AudioSourceEditing::validate(*entry.current, edit);
        if (!problem.isEmpty()) {
            // An edit that no longer fits (a relinked, shorter file) is reported
            // and skipped; the later edits still apply where they fit.
            entry.problems.push_back(tr("%1: %2").arg(edit.label(), problem));
            continue;
        }
        entry.current = AudioSourceEditing::apply(*entry.current, edit);
    }
}

AudioBufferPtr AudioSourceEditModel::buffer(const QString &resourceId)
{
    Source *entry = source(resourceId);
    if (!entry || !entry->original) return nullptr;
    if (m_preview.state == QLatin1String("ready") && m_preview.resourceId == resourceId) return m_preview.result;
    fold(*entry);
    return entry->current;
}

QString AudioSourceEditModel::frames(const QString &resourceId)
{
    const AudioBufferPtr samples = buffer(resourceId);
    return QString::number(samples ? samples->frames : 0);
}

int AudioSourceEditModel::channels(const QString &resourceId)
{
    const AudioBufferPtr samples = buffer(resourceId);
    return samples ? samples->channels : 0;
}

int AudioSourceEditModel::sampleRate(const QString &resourceId)
{
    const AudioBufferPtr samples = buffer(resourceId);
    return samples ? samples->sampleRate : 0;
}

QVariantList AudioSourceEditModel::edits(const QString &resourceId) const
{
    QVariantList list;
    for (const AudioSourceEdit &edit : m_sources.value(resourceId).edits) {
        QVariantMap map = edit.toMap();
        map.insert(QStringLiteral("label"), edit.label());
        list.push_back(map);
    }
    return list;
}

QVariantList AudioSourceEditModel::capabilities(const QString &resourceId)
{
    const AudioBufferPtr samples = buffer(resourceId);
    return samples ? AudioSourceEditing::capabilities(*samples) : QVariantList{};
}

QStringList AudioSourceEditModel::problems(const QString &resourceId) const
{
    return m_sources.value(resourceId).problems;
}

QVariantMap AudioSourceEditModel::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

void AudioSourceEditModel::push(const QString &resourceId, AudioSourceEdit edit, AudioBufferPtr result)
{
    Source &entry = m_sources[resourceId];
    m_history.resize(m_cursor); // an edit after undo discards the redo steps
    entry.edits.push_back(edit);
    entry.current = std::move(result);
    entry.folded = int(entry.edits.size());
    m_history.push_back({resourceId, std::move(edit)});
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = m_history.size();
    touch(resourceId);
    emit historyChanged();
}

QVariantMap AudioSourceEditModel::apply(const QString &resourceId, const QVariantMap &map)
{
    QString error;
    auto edit = AudioSourceEdit::fromMap(map, &error);
    if (!edit) return fail(error);
    dropPreview(resourceId);
    const AudioBufferPtr base = buffer(resourceId);
    if (!base) return fail(tr("The source %1 has no decoded samples").arg(resourceId));
    if (!AudioSourceEditing::prepare(*base, *edit, {}, &error)) return fail(error);
    const AudioBufferPtr result = AudioSourceEditing::apply(*base, *edit);
    push(resourceId, std::move(*edit), result);
    return success(revisionNumber(resourceId), result);
}

QVariantMap AudioSourceEditModel::preview(const QString &requestId, const QString &resourceId, const QVariantMap &map)
{
    QString error;
    auto edit = AudioSourceEdit::fromMap(map, &error);
    if (!edit) return fail(error);
    if (requestId.isEmpty()) return fail(tr("A preview needs a request ID"));
    dropPreview({});
    const AudioBufferPtr base = buffer(resourceId);
    if (!base) return fail(tr("The source %1 has no decoded samples").arg(resourceId));
    if (const QString problem = AudioSourceEditing::validate(*base, *edit); !problem.isEmpty()) return fail(problem);
    m_preview = Preview{};
    m_preview.requestId = requestId;
    m_preview.resourceId = resourceId;
    m_preview.state = QStringLiteral("computing");
    m_preview.baseRevision = revisionNumber(resourceId);
    m_preview.serial = ++m_previewSerial;
    m_preview.edit = *edit;
    const CancellationToken token = m_preview.cancel.token();
    const quint64 serial = m_preview.serial;
    auto *watcher = new QFutureWatcher<PreviewResult>(this);
    connect(watcher, &QFutureWatcher<PreviewResult>::finished, this, [this, watcher, serial] {
        const PreviewResult result = watcher->result();
        watcher->deleteLater();
        finishPreview(serial, result);
    });
    // AGENT-NOTE (D-137): heavy processing never runs on the GUI or audio
    // thread; the worker reads an immutable buffer and checks the token
    // between analysis frames.
    watcher->setFuture(QtConcurrent::run([base, pending = *edit, token]() mutable {
        PreviewResult result;
        result.ok = AudioSourceEditing::prepare(*base, pending, token, &result.error);
        if (result.ok && !token.isCancelled()) result.result = AudioSourceEditing::apply(*base, pending);
        result.ok = result.ok && result.result;
        result.edit = std::move(pending);
        return result;
    }));
    emit previewChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("requestId"), requestId},
            {QStringLiteral("state"), m_preview.state}};
}

void AudioSourceEditModel::setPreviewState(const QString &state)
{
    m_preview.state = state;
    emit previewChanged();
}

void AudioSourceEditModel::finishPreview(quint64 serial, const PreviewResult &result)
{
    // A superseded or cancelled preview is discarded whatever it produced.
    if (serial != m_preview.serial || m_preview.state != QLatin1String("computing")) return;
    const QString requestId = m_preview.requestId;
    const QString resourceId = m_preview.resourceId;
    QString error = result.error;
    if (revisionNumber(resourceId) != m_preview.baseRevision)
        error = tr("The source changed while the preview was computed");
    else if (!result.ok && error.isEmpty())
        error = tr("The preview could not be computed");
    if (!error.isEmpty() || !result.ok) {
        setPreviewState(QStringLiteral("failed"));
        emit previewFailed(requestId, error);
        return;
    }
    m_preview.edit = result.edit;
    m_preview.result = result.result;
    setPreviewState(QStringLiteral("ready"));
    touch(resourceId); // the previewed samples are now what plays and is analysed
    const qint64 patchEnd = result.edit.patch ? result.edit.patchStart + result.edit.patch->frames : result.edit.end;
    emit previewReady(requestId, {{QStringLiteral("resourceId"), resourceId},
                                  {QStringLiteral("operation"), result.edit.operation()},
                                  {QStringLiteral("label"), result.edit.label()},
                                  {QStringLiteral("revision"), revision(resourceId)},
                                  {QStringLiteral("frames"), QString::number(result.result->frames)},
                                  {QStringLiteral("changedStartSample"),
                                   QString::number(result.edit.patch ? result.edit.patchStart : result.edit.start)},
                                  {QStringLiteral("changedEndSample"), QString::number(patchEnd)}});
}

QVariantMap AudioSourceEditModel::commitPreview(const QString &requestId)
{
    if (requestId.isEmpty() || requestId != m_preview.requestId) return fail(tr("No preview %1").arg(requestId));
    if (m_preview.state == QLatin1String("computing")) return fail(tr("The preview is still being computed"));
    if (m_preview.state != QLatin1String("ready")) return fail(tr("The preview %1 is %2").arg(requestId, m_preview.state));
    const QString resourceId = m_preview.resourceId;
    AudioSourceEdit edit = m_preview.edit;
    const AudioBufferPtr result = m_preview.result;
    m_preview.result.reset();
    setPreviewState(QStringLiteral("committed"));
    push(resourceId, std::move(edit), result);
    return success(revisionNumber(resourceId), result);
}

bool AudioSourceEditModel::cancelPreview(const QString &requestId)
{
    if (requestId.isEmpty() || requestId != m_preview.requestId || !active(m_preview.state)) return false;
    const bool wasReady = m_preview.state == QLatin1String("ready");
    m_preview.cancel.cancel();
    m_preview.result.reset();
    setPreviewState(QStringLiteral("canceled"));
    emit previewCanceled(requestId);
    if (wasReady) touch(m_preview.resourceId); // back to the committed samples
    return true;
}

void AudioSourceEditModel::dropPreview(const QString &resourceId)
{
    if (active(m_preview.state) && (resourceId.isEmpty() || resourceId == m_preview.resourceId))
        cancelPreview(m_preview.requestId);
}

bool AudioSourceEditModel::undo()
{
    if (!canUndo()) return false;
    const QString resourceId = m_history.at(m_cursor - 1).resourceId;
    dropPreview(resourceId); // may emit; look the source up afterwards
    Source *entry = source(resourceId);
    if (!entry || entry->edits.isEmpty()) return false;
    entry->edits.removeLast();
    entry->current.reset(); // refolded from the original on next use; patches are kept
    entry->folded = 0;
    --m_cursor;
    touch(resourceId);
    emit historyChanged();
    return true;
}

bool AudioSourceEditModel::redo()
{
    if (!canRedo()) return false;
    dropPreview(m_history.at(m_cursor).resourceId);
    Step &step = m_history[m_cursor];
    const AudioBufferPtr base = buffer(step.resourceId);
    QString error;
    if (!base) {
        fail(tr("The source %1 has no decoded samples").arg(step.resourceId));
        return false;
    }
    if (!step.edit.patch && !AudioSourceEditing::prepare(*base, step.edit, {}, &error)) {
        fail(error);
        return false;
    }
    if (const QString problem = AudioSourceEditing::validate(*base, step.edit); !problem.isEmpty()) {
        fail(problem);
        return false;
    }
    Source &entry = m_sources[step.resourceId];
    entry.edits.push_back(step.edit);
    entry.current = AudioSourceEditing::apply(*base, step.edit);
    entry.folded = int(entry.edits.size());
    ++m_cursor;
    touch(step.resourceId);
    emit historyChanged();
    return true;
}

void AudioSourceEditModel::clearHistory()
{
    if (m_history.isEmpty()) return;
    m_history.clear();
    m_cursor = 0;
    emit historyChanged();
}

QVariantMap AudioSourceEditModel::contentMap() const
{
    QVariantMap sources;
    for (auto it = m_sources.cbegin(); it != m_sources.cend(); ++it) {
        if (it->edits.isEmpty()) continue;
        QVariantList list;
        for (const AudioSourceEdit &edit : it->edits) list.push_back(edit.toMap());
        sources.insert(it.key(), list);
    }
    return {{QStringLiteral("sources"), sources}};
}

QVariantMap AudioSourceEditModel::loadContent(const QVariantMap &content)
{
    // Validate everything before replacing anything (session open is all or nothing).
    QHash<QString, QVector<AudioSourceEdit>> loaded;
    const QVariantMap sources = content.value(QStringLiteral("sources")).toMap();
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        for (const QVariant &value : it.value().toList()) {
            QString error;
            const auto edit = AudioSourceEdit::fromMap(value.toMap(), &error);
            if (!edit) return fail(tr("%1: %2").arg(it.key(), error));
            loaded[it.key()].push_back(*edit);
        }
    }
    clearSources();
    for (auto it = loaded.cbegin(); it != loaded.cend(); ++it) m_sources[it.key()].edits = it.value();
    return {{QStringLiteral("ok"), true}};
}

} // namespace QindaTK
