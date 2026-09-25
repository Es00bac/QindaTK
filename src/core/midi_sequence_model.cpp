// SPDX-License-Identifier: LGPL-3.0-or-later
// MidiSequenceModel: content, history, persistence and the control snapshots.
// Operations are in midi_sequence_edit.cpp (notes, events, gestures) and
// midi_sequence_ops.cpp (patterns, rows, steps, scenes, slots).
#include "midi_sequence_model.h"

#include "midi_input_port.h"
#include "timeline_time.h"

#include <QJsonDocument>
#include <algorithm>
#include <numeric>

namespace QindaTK {
namespace {

QString decimal(qint64 value)
{
    return QString::number(value);
}

double fraction(qint64 tick, qint64 start, qint64 span)
{
    return std::clamp(double(tick - start) / double(span), 0.0, 1.0);
}

// Every tick of `content` at `to` PPQ instead of `from`; false (content
// partly changed) when one position is not exact or leaves the valid range.
bool rescale(MidiContent &content, int from, int to)
{
    const qint64 divisor = std::gcd(from, to);
    const qint64 num = to / divisor;
    const qint64 den = from / divisor;
    const auto scale = [&](qint64 &value) {
        if (value > Midi::kMaxLength || (value * num) % den != 0) return false; // |value| <= 2^40, num <= 10^6
        value = value * num / den;
        return true;
    };
    for (MidiPattern &pattern : content.patterns) {
        if (!scale(pattern.length)) return false;
        for (MidiNote &note : pattern.notes)
            if (!scale(note.tick) || !scale(note.duration)) return false;
        for (MidiControlEvent &event : pattern.events)
            if (!scale(event.tick)) return false;
        for (MidiStepRow &row : pattern.rows)
            if (!scale(row.stepTicks)) return false;
    }
    return Midi::validate(content).isEmpty();
}

} // namespace

MidiSequenceModel::MidiSequenceModel(QObject *parent)
    : QObject(parent)
{
}

QString MidiSequenceModel::ticksPerQuarterProblem(int ppq) const
{
    if (ppq < 1 || ppq > 1000000) return tr("Ticks per quarter must be 1..1000000");
    if (ppq == m_ppq) return {};
    if (m_gesture) return tr("Finish or cancel the current MIDI edit first");
    const QString inexact = tr("The MIDI notes would move: a position is not exact at %1 ticks per quarter").arg(ppq);
    MidiContent content = m_content;
    if (!rescale(content, m_ppq, ppq)) return inexact;
    for (const Step &step : m_history) {
        MidiContent before = step.before, after = step.after;
        if (!rescale(before, m_ppq, ppq) || !rescale(after, m_ppq, ppq)) return inexact;
    }
    return {};
}

void MidiSequenceModel::setTicksPerQuarter(int ppq)
{
    if (ppq == m_ppq) return;
    if (const QString problem = ticksPerQuarterProblem(ppq); !problem.isEmpty()) {
        failure(problem);
        return;
    }
    const int from = m_ppq;
    rescale(m_content, from, ppq);
    for (Step &step : m_history) {
        rescale(step.before, from, ppq);
        rescale(step.after, from, ppq);
    }
    m_ppq = ppq;
    emit ticksPerQuarterChanged();
    publish();
}

QString MidiSequenceModel::removalProblem(const MidiContent &next) const
{
    if (!m_patternUse) return {};
    for (const MidiPattern &pattern : m_content.patterns)
        if (!Midi::find(next, pattern.id))
            if (const QString use = m_patternUse(pattern.id); !use.isEmpty()) return use;
    return {};
}

bool MidiSequenceModel::commitContent(const MidiContent &next, const QString &label, QString *error)
{
    QString problem = Midi::validate(next);
    if (problem.isEmpty()) problem = removalProblem(next);
    if (!problem.isEmpty()) {
        if (error) *error = problem;
        return false;
    }
    if (next == m_content) return true;
    m_history.resize(m_cursor);
    m_history.push_back({label, m_content, next});
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = m_history.size();
    m_content = next;
    publish();
    emit historyChanged();
    emit committed(label);
    return true;
}

void MidiSequenceModel::publish()
{
    ++m_revision;
    emit contentChanged();
}

QVariantMap MidiSequenceModel::ok(const QVariantMap &extra)
{
    QVariantMap result = extra;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("revision"), revisionText());
    return result;
}

QVariantMap MidiSequenceModel::failure(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

void MidiSequenceModel::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

QVariantMap MidiSequenceModel::editContent(const QString &label, const std::function<QString(MidiContent &)> &edit,
                                           const QVariantMap &extra)
{
    clearError();
    if (m_gesture) return failure(tr("Finish or cancel the current edit first"));
    MidiContent next = m_content;
    if (const QString problem = edit(next); !problem.isEmpty()) return failure(problem);
    for (MidiPattern &pattern : next.patterns) Midi::sortPattern(pattern);
    QString error;
    if (!commitContent(next, label, &error)) return failure(error);
    return ok(extra);
}

QVariantMap MidiSequenceModel::editPattern(const QString &patternId, const QString &label,
                                           const std::function<QString(MidiPattern &)> &edit, const QVariantMap &extra)
{
    const QUuid id = QUuid::fromString(patternId);
    return editContent(label, [&](MidiContent &content) {
        for (MidiPattern &pattern : content.patterns)
            if (pattern.id == id) return edit(pattern);
        return tr("There is no pattern %1").arg(patternId);
    }, extra);
}

const MidiPattern *MidiSequenceModel::viewPattern(const QUuid &id) const
{
    if (m_gesture && m_gesture->patternId == id) return &m_gesture->preview;
    return pattern(id);
}

QVariantList MidiSequenceModel::patterns() const
{
    QVariantList list;
    for (const MidiPattern &pattern : m_content.patterns)
        list.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(pattern.id)},
                                   {QStringLiteral("resourceId"), Midi::patternResourceId(pattern.id)},
                                   {QStringLiteral("name"), pattern.name},
                                   {QStringLiteral("length"), decimal(pattern.length)},
                                   {QStringLiteral("seed"), double(pattern.seed)},
                                   {QStringLiteral("noteCount"), int(pattern.notes.size())},
                                   {QStringLiteral("eventCount"), int(pattern.events.size())},
                                   {QStringLiteral("rowCount"), int(pattern.rows.size())}});
    return list;
}

QVariantList MidiSequenceModel::scenes() const
{
    QVariantList list;
    for (const MidiScene &scene : m_content.scenes) {
        QVariantList cells;
        for (const MidiSlot &slot : m_content.launchSlots)
            if (slot.sceneId == scene.id)
                cells.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(slot.id)},
                                            {QStringLiteral("trackId"), Midi::idText(slot.trackId)},
                                            {QStringLiteral("patternId"), Midi::idText(slot.patternId)},
                                            {QStringLiteral("label"), slot.label}});
        list.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(scene.id)}, {QStringLiteral("name"), scene.name},
                                   {QStringLiteral("slots"), cells}});
    }
    return list;
}

void MidiSequenceModel::setQuantization(const QString &name)
{
    const auto quantize = Midi::parseQuantize(name);
    if (!quantize) {
        failure(tr("Launch quantization is none, beat or bar"));
        return;
    }
    editContent(tr("Set launch quantization"), [&](MidiContent &content) {
        content.quantize = *quantize;
        return QString();
    });
}

QVariantMap MidiSequenceModel::capabilities() const
{
    const QVariantMap smf{{QStringLiteral("importFormats"), QVariantList{0, 1}},
                          {QStringLiteral("exportFormat"), 1},
                          {QStringLiteral("notes"), true},
                          {QStringLiteral("controlChange"), true},
                          {QStringLiteral("pitchBend"), true},
                          {QStringLiteral("channelPressure"), true},
                          {QStringLiteral("tempo"), true},
                          {QStringLiteral("meter"), true},
                          {QStringLiteral("velocityBits"), 7},
                          {QStringLiteral("perNoteExpression"), false},
                          {QStringLiteral("steps"), false}};
    return {{QStringLiteral("ticksPerQuarter"), m_ppq},
            {QStringLiteral("velocityBits"), 16},
            {QStringLiteral("controllerBits"), 32},
            {QStringLiteral("controllers"), Midi::descriptors()},
            {QStringLiteral("perNoteExpression"), true},
            {QStringLiteral("polymetricSteps"), true},
            {QStringLiteral("probability"), QStringLiteral("seeded")},
            {QStringLiteral("launchQuantization"), QStringList{QStringLiteral("none"), QStringLiteral("beat"),
                                                               QStringLiteral("bar")}},
            {QStringLiteral("instrument"), QStringLiteral("builtin-test")},
            {QStringLiteral("smf"), smf},
            {QStringLiteral("midiDevices"),
             QVariantMap{{QStringLiteral("input"), MidiInputPort::available()},
                         {QStringLiteral("inputTransport"), QStringLiteral("alsa-sequencer")},
                         {QStringLiteral("output"), false},
                         {QStringLiteral("recording"), true}}}};
}

QVariantList MidiSequenceModel::noteRows(const QString &patternId, const QString &startTicks,
                                         const QString &endTicks) const
{
    const MidiPattern *source = viewPattern(QUuid::fromString(patternId));
    const auto start = TimelineTime::parseDecimal(startTicks);
    const auto end = TimelineTime::parseDecimal(endTicks);
    if (!source || !start || !end || *end <= *start) return {};
    const qint64 span = *end - *start;
    QVariantList rows;
    for (const MidiNote &note : source->notes) {
        if (note.end() <= *start || note.tick >= *end) continue;
        rows.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(note.id)},
                                   {QStringLiteral("pitch"), note.pitch},
                                   {QStringLiteral("channel"), note.channel},
                                   {QStringLiteral("velocity"), Midi::velocityToUnit(note.velocity)},
                                   {QStringLiteral("velocity16"), int(note.velocity)},
                                   {QStringLiteral("x0"), fraction(note.tick, *start, span)},
                                   {QStringLiteral("x1"), fraction(note.end(), *start, span)},
                                   {QStringLiteral("startTicks"), decimal(note.tick)},
                                   {QStringLiteral("endTicks"), decimal(note.end())}});
    }
    return rows;
}

QStringList MidiSequenceModel::tickBoundaries(const QString &startTicks, const QString &endTicks,
                                              const QString &gridTicks) const
{
    const auto start = TimelineTime::parseDecimal(startTicks);
    const auto end = TimelineTime::parseDecimal(endTicks);
    const auto grid = TimelineTime::parseDecimal(gridTicks);
    if (!start || !end || !grid || *grid < 1 || *end <= *start || (*end - *start) / *grid > 4096) return {};
    QStringList boundaries;
    for (qint64 tick = *start; tick <= *end; tick += *grid) boundaries.push_back(decimal(tick));
    return boundaries;
}

QVariantList MidiSequenceModel::controllerEvents(const QString &patternId, const QString &controllerId,
                                                 const QString &startTicks, const QString &endTicks) const
{
    const MidiPattern *source = viewPattern(QUuid::fromString(patternId));
    const auto start = TimelineTime::parseDecimal(startTicks);
    const auto end = TimelineTime::parseDecimal(endTicks);
    if (!source || !start || !end || *end <= *start) return {};
    const qint64 span = *end - *start;
    QVariantList list;
    if (controllerId == QLatin1String("velocity")) {
        for (const MidiNote &note : source->notes)
            if (note.tick >= *start && note.tick < *end)
                list.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(note.id)},
                                           {QStringLiteral("ticks"), decimal(note.tick)},
                                           {QStringLiteral("value"), Midi::velocityToUnit(note.velocity)},
                                           {QStringLiteral("raw"), decimal(note.velocity)},
                                           {QStringLiteral("x"), fraction(note.tick, *start, span)},
                                           {QStringLiteral("noteId"), Midi::idText(note.id)}});
        return list;
    }
    const auto controller = Midi::parseController(controllerId);
    if (!controller) return {};
    for (const MidiControlEvent &event : source->events) {
        if (event.kind != controller->first || event.number != controller->second) continue;
        if (event.tick < *start || event.tick >= *end) continue;
        list.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(event.id)},
                                   {QStringLiteral("ticks"), decimal(event.tick)},
                                   {QStringLiteral("value"), Midi::valueToUnit(event.kind, event.value)},
                                   {QStringLiteral("raw"), decimal(event.value)},
                                   {QStringLiteral("x"), fraction(event.tick, *start, span)},
                                   {QStringLiteral("channel"), event.channel},
                                   {QStringLiteral("noteId"), Midi::idText(event.noteId)}});
    }
    return list;
}

QVariantList MidiSequenceModel::stepRows(const QString &patternId) const
{
    const MidiPattern *source = viewPattern(QUuid::fromString(patternId));
    if (!source) return {};
    QVariantList rows;
    for (const MidiStepRow &row : source->rows) {
        QVariantList steps;
        for (const MidiStep &step : row.steps)
            steps.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(step.id)},
                                        {QStringLiteral("enabled"), step.enabled},
                                        {QStringLiteral("velocity"), Midi::velocityToUnit(step.velocity)},
                                        {QStringLiteral("gate"), step.gate},
                                        {QStringLiteral("probability"), step.probability}});
        rows.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(row.id)}, {QStringLiteral("name"), row.name},
                                   {QStringLiteral("pitch"), row.pitch}, {QStringLiteral("channel"), row.channel},
                                   {QStringLiteral("stepTicks"), decimal(row.stepTicks)},
                                   {QStringLiteral("steps"), steps}});
    }
    return rows;
}

bool MidiSequenceModel::undo()
{
    if (m_gesture || !canUndo()) return false;
    if (const QString problem = removalProblem(m_history.at(m_cursor - 1).before); !problem.isEmpty()) {
        failure(problem);
        return false;
    }
    clearError();
    m_content = m_history.at(--m_cursor).before;
    publish();
    emit historyChanged();
    return true;
}

bool MidiSequenceModel::redo()
{
    if (m_gesture || !canRedo()) return false;
    if (const QString problem = removalProblem(m_history.at(m_cursor).after); !problem.isEmpty()) {
        failure(problem);
        return false;
    }
    clearError();
    m_content = m_history.at(m_cursor++).after;
    publish();
    emit historyChanged();
    return true;
}

void MidiSequenceModel::clearHistory()
{
    m_history.clear();
    m_cursor = 0;
    emit historyChanged();
}

QVariantMap MidiSequenceModel::contentMap() const
{
    return Midi::toJson(m_content, m_ppq).toVariantMap();
}

QVariantMap MidiSequenceModel::loadContent(const QVariantMap &content)
{
    clearError();
    QString error;
    const auto parsed = Midi::fromJson(QJsonObject::fromVariantMap(content), m_ppq, &error);
    if (!parsed) return failure(error);
    if (m_gesture) {
        m_gesture.reset();
        emit previewChanged();
    }
    m_content = *parsed;
    m_history.clear();
    m_cursor = 0;
    publish();
    emit historyChanged();
    return ok();
}

QString MidiSequenceModel::toJson() const
{
    return QString::fromUtf8(QJsonDocument(Midi::toJson(m_content, m_ppq)).toJson(QJsonDocument::Compact));
}

QVariantMap MidiSequenceModel::loadJson(const QString &json)
{
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parse);
    if (!document.isObject()) return failure(tr("The MIDI content is not valid JSON: %1").arg(parse.errorString()));
    return loadContent(document.object().toVariantMap());
}

} // namespace QindaTK
