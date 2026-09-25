// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioTakeCompModel (WP-065): comps of a session, their history, previews,
// audition and persistence. The view snapshot is in audio_take_comp_view.cpp.
#include "audio_take_comp_model.h"

#include "timeline_time.h"

#include <QSet>

namespace QindaTK {
namespace {

QVariantMap ok(bool changed)
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), changed}};
}

std::optional<qint64> sample(const QString &text)
{
    return TimelineTime::parseDecimal(text);
}

} // namespace

AudioTakeCompModel::AudioTakeCompModel(QObject *parent)
    : QObject(parent)
{
}

const AudioTakeComp *AudioTakeCompModel::comp(const QString &compId) const
{
    const auto it = m_comps.constFind(compId);
    return it == m_comps.cend() ? nullptr : &it.value();
}

AudioCompPlayback AudioTakeCompModel::playback(const QString &compId) const
{
    const AudioTakeComp *content = m_preview.active && m_preview.comp.id == compId ? &m_preview.comp : comp(compId);
    if (!content) return {};
    const QString take = m_listening.value(compId).take;
    return AudioComp::playback(*content, content->take(take) ? take : QString());
}

QStringList AudioTakeCompModel::compIds() const
{
    return m_order;
}

bool AudioTakeCompModel::insert(const AudioTakeComp &comp, QString *error)
{
    QString problem = AudioComp::validate(comp);
    if (problem.isEmpty() && m_comps.contains(comp.id)) problem = tr("Comp %1 already exists").arg(comp.id);
    if (!problem.isEmpty()) {
        if (error) *error = problem;
        return false;
    }
    m_comps.insert(comp.id, comp);
    m_order.push_back(comp.id);
    touch();
    return true;
}

bool AudioTakeCompModel::remove(const QString &compId)
{
    if (!m_comps.remove(compId)) return false;
    m_order.removeAll(compId);
    m_listening.remove(compId);
    if (m_preview.active && m_preview.comp.id == compId) m_preview = {};
    touch();
    return true;
}

bool AudioTakeCompModel::record(const QString &compId, const QVector<AudioTake> &takes,
                                const QVector<Selection> &selections, QString *error)
{
    const QVariantMap result = edit(compId, tr("Record takes"), [&](AudioTakeComp &next) {
        QString problem = AudioComp::addTakes(next, takes);
        for (const Selection &selection : selections) {
            if (!problem.isEmpty()) break;
            problem = AudioComp::select(next, selection.takeId, selection.start, selection.end);
        }
        return problem;
    });
    if (error) *error = result.value(QStringLiteral("error")).toString();
    return result.value(QStringLiteral("ok")).toBool();
}

void AudioTakeCompModel::sourcesChanged()
{
    ++m_revision;
    emit revisionChanged();
}

QString AudioTakeCompModel::undoText() const
{
    return canUndo() ? m_history.at(m_cursor - 1).label : QString();
}

QString AudioTakeCompModel::redoText() const
{
    return canRedo() ? m_history.at(m_cursor).label : QString();
}

QVariantMap AudioTakeCompModel::compData(const QString &compId) const
{
    const AudioTakeComp *content = comp(compId);
    if (!content) return {};
    QVariantMap map = AudioComp::toMap(*content);
    map.insert(QStringLiteral("audition"), audition(compId));
    map.insert(QStringLiteral("previewing"), previewCompId() == compId);
    return map;
}

QVariantList AudioTakeCompModel::pieces(const QString &compId) const
{
    QVariantList list;
    for (const AudioCompPlayback::Part &part : playback(compId).parts) list.push_back(AudioComp::pieceMap(part.piece));
    return list;
}

QVariantMap AudioTakeCompModel::edit(const QString &compId, const QString &label, const Edit &change)
{
    clearError();
    dropPreview();
    const auto it = m_comps.find(compId);
    if (it == m_comps.end()) return fail(tr("There is no comp %1").arg(compId));
    AudioTakeComp next = it.value();
    QString problem = change(next);
    if (problem.isEmpty()) problem = AudioComp::validate(next);
    if (!problem.isEmpty()) return fail(problem);
    if (next == it.value()) return ok(false);
    const AudioTakeComp before = it.value();
    m_comps.insert(compId, next);
    push(label, before, next);
    touch();
    return ok(true);
}

QVariantMap AudioTakeCompModel::select(const QString &compId, const QString &takeId, const QString &startSample,
                                       const QString &endSample)
{
    const auto start = sample(startSample);
    const auto end = sample(endSample);
    if (!start || !end) return fail(tr("Sample positions must be decimal integers"));
    return edit(compId, tr("Select take"),
                [&](AudioTakeComp &next) { return AudioComp::select(next, takeId, *start, *end); });
}

QVariantMap AudioTakeCompModel::split(const QString &compId, const QString &atSample)
{
    const auto at = sample(atSample);
    if (!at) return fail(tr("Sample positions must be decimal integers"));
    return edit(compId, tr("Split comp segment"), [&](AudioTakeComp &next) { return AudioComp::split(next, *at); });
}

QVariantMap AudioTakeCompModel::merge(const QString &compId, const QString &segmentId)
{
    return edit(compId, tr("Merge comp segments"),
                [&](AudioTakeComp &next) { return AudioComp::merge(next, segmentId); });
}

QVariantMap AudioTakeCompModel::clear(const QString &compId, const QString &startSample, const QString &endSample)
{
    const auto start = sample(startSample);
    const auto end = sample(endSample);
    if (!start || !end) return fail(tr("Sample positions must be decimal integers"));
    return edit(compId, tr("Clear comp range"),
                [&](AudioTakeComp &next) { return AudioComp::clear(next, *start, *end); });
}

QVariantMap AudioTakeCompModel::setFade(const QString &compId, const QString &samples, const QString &shape)
{
    const auto length = sample(samples);
    if (!length) return fail(tr("Sample positions must be decimal integers"));
    if (shape != QLatin1String("linear") && shape != QLatin1String("equalPower"))
        return fail(tr("The crossfade shape must be linear or equalPower"));
    const AudioCompFade fade = shape == QLatin1String("linear") ? AudioCompFade::Linear : AudioCompFade::EqualPower;
    return edit(compId, tr("Set comp crossfade"),
                [&](AudioTakeComp &next) { return AudioComp::setFade(next, *length, fade); });
}

QVariantMap AudioTakeCompModel::preview(const QString &compId, const QString &takeId, const QString &startSample,
                                        const QString &endSample)
{
    clearError();
    const auto start = sample(startSample);
    const auto end = sample(endSample);
    const AudioTakeComp *base = comp(compId);
    if (!base) return fail(tr("There is no comp %1").arg(compId));
    if (!start || !end) return fail(tr("Sample positions must be decimal integers"));
    // AGENT-GUARD: a preview always starts from the committed comp, never
    // from the previous preview, so a swipe that shrinks back leaves nothing
    // of its wider range behind. A refused update keeps the last preview.
    AudioTakeComp next = *base;
    if (const QString problem = AudioComp::select(next, takeId, *start, *end); !problem.isEmpty()) return fail(problem);
    if (m_preview.active && m_preview.comp.id != compId) dropPreview();
    m_preview = {true, next};
    touch();
    return ok(true);
}

QVariantMap AudioTakeCompModel::commitPreview()
{
    clearError();
    if (!m_preview.active) return fail(tr("There is no comp preview to commit"));
    const AudioTakeComp after = m_preview.comp;
    m_preview = {};
    const auto it = m_comps.find(after.id);
    if (it == m_comps.end()) {
        touch();
        return fail(tr("There is no comp %1").arg(after.id));
    }
    const bool changed = !(after == it.value());
    if (changed) {
        const AudioTakeComp before = it.value();
        m_comps.insert(after.id, after);
        push(tr("Select take"), before, after);
    }
    touch();
    return ok(changed);
}

bool AudioTakeCompModel::cancelPreview()
{
    if (!m_preview.active) return false;
    dropPreview();
    return true;
}

bool AudioTakeCompModel::setAudition(const QString &compId, const QString &takeId)
{
    const AudioTakeComp *content = comp(compId);
    if (!content || (!takeId.isEmpty() && !content->take(takeId))) return false;
    m_listening.insert(compId, {takeId, {}, {}});
    touch();
    return true;
}

QString AudioTakeCompModel::audition(const QString &compId) const
{
    const AudioTakeComp *content = comp(compId);
    const QString take = m_listening.value(compId).take;
    return content && content->take(take) ? take : QString();
}

bool AudioTakeCompModel::compare(const QString &compId, const QString &takeId, const QString &referenceTakeId)
{
    const AudioTakeComp *content = comp(compId);
    if (!content || !content->take(takeId) || !content->take(referenceTakeId) || takeId == referenceTakeId) return false;
    m_listening.insert(compId, {takeId, takeId, referenceTakeId});
    touch();
    return true;
}

QString AudioTakeCompModel::toggleComparison(const QString &compId)
{
    const auto it = m_listening.find(compId);
    const AudioTakeComp *content = comp(compId);
    if (it == m_listening.end() || !content || !content->take(it->a) || !content->take(it->b)) return {};
    it->take = it->take == it->a ? it->b : it->a;
    touch();
    return it->take;
}

bool AudioTakeCompModel::undo()
{
    dropPreview();
    if (!canUndo()) return false;
    --m_cursor;
    restore(m_history.at(m_cursor).before);
    emit historyChanged();
    return true;
}

bool AudioTakeCompModel::redo()
{
    dropPreview();
    if (!canRedo()) return false;
    restore(m_history.at(m_cursor).after);
    ++m_cursor;
    emit historyChanged();
    return true;
}

void AudioTakeCompModel::clearHistory()
{
    if (m_history.isEmpty()) return;
    m_history.clear();
    m_cursor = 0;
    emit historyChanged();
}

QVariantMap AudioTakeCompModel::contentMap() const
{
    QVariantList comps;
    for (const QString &id : m_order) comps.push_back(AudioComp::toMap(m_comps.value(id)));
    return {{QStringLiteral("comps"), comps}};
}

QVariantMap AudioTakeCompModel::loadContent(const QVariantMap &content)
{
    clearError();
    QHash<QString, AudioTakeComp> comps;
    QStringList order;
    for (const QVariant &value : content.value(QStringLiteral("comps")).toList()) {
        QString problem;
        const auto comp = AudioComp::fromMap(value.toMap(), &problem);
        if (!comp) return fail(problem);
        if (comps.contains(comp->id)) return fail(tr("Comp %1 is defined twice").arg(comp->id));
        comps.insert(comp->id, *comp);
        order.push_back(comp->id);
    }
    m_preview = {};
    m_listening.clear();
    m_comps = comps;
    m_order = order;
    clearHistory();
    touch();
    return ok(true);
}

void AudioTakeCompModel::push(const QString &label, const AudioTakeComp &before, const AudioTakeComp &after)
{
    m_history.resize(m_cursor);
    m_history.push_back({label, before, after});
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = m_history.size();
    emit historyChanged();
}

void AudioTakeCompModel::restore(const AudioTakeComp &comp)
{
    if (!m_comps.contains(comp.id)) m_order.push_back(comp.id);
    m_comps.insert(comp.id, comp);
    touch();
}

void AudioTakeCompModel::dropPreview()
{
    if (!m_preview.active) return;
    m_preview = {};
    touch();
}

void AudioTakeCompModel::touch()
{
    ++m_revision;
    emit changed();
    emit revisionChanged();
}

QVariantMap AudioTakeCompModel::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

void AudioTakeCompModel::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

} // namespace QindaTK
