// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tempo_map_model.h"

#include "timeline_time.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaTK {
namespace {

QVariantMap failure(const OperationError &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error.message},
            {QStringLiteral("code"), int(error.code)}, {QStringLiteral("field"), error.fieldPath}};
}

OperationError argument(const QString &message)
{
    return {ErrorCode::InvalidArgument, message, {}, {}};
}

bool isAdd(TempoEditKind kind)
{
    return kind == TempoEditKind::AddTempo || kind == TempoEditKind::AddMeter;
}

QString defaultLabel(TempoEditKind kind, const TempoEvent *target)
{
    const bool meter = kind == TempoEditKind::AddMeter
        || (target && target->kind == TempoEvent::Kind::Meter);
    switch (kind) {
    case TempoEditKind::AddTempo: return TempoMapModel::tr("Add tempo change");
    case TempoEditKind::AddMeter: return TempoMapModel::tr("Add meter change");
    case TempoEditKind::Move: return meter ? TempoMapModel::tr("Move meter change") : TempoMapModel::tr("Move tempo change");
    case TempoEditKind::Edit: return meter ? TempoMapModel::tr("Change meter") : TempoMapModel::tr("Change tempo");
    case TempoEditKind::Remove: return meter ? TempoMapModel::tr("Remove meter change") : TempoMapModel::tr("Remove tempo change");
    }
    return {};
}

// Decimal-string ticks and tempo, integer meter parts, or a BPM number/text.
bool readFields(const TempoMapModel &model, const QVariantMap &map, TempoEventFields &fields, QString &error)
{
    if (map.contains(QStringLiteral("tick"))) {
        const QVariant value = map.value(QStringLiteral("tick"));
        fields.tick = value.metaType().id() == QMetaType::QString
                          ? TimelineTime::parseDecimal(value.toString()) : std::nullopt;
        if (!fields.tick) return (error = TempoMapModel::tr("tick must be an exact decimal string")), false;
    }
    if (map.contains(QStringLiteral("microsecondsPerQuarter"))) {
        const QVariant value = map.value(QStringLiteral("microsecondsPerQuarter"));
        fields.microsecondsPerQuarter = value.metaType().id() == QMetaType::QString
                                            ? TimelineTime::parseDecimal(value.toString()) : std::nullopt;
        if (!fields.microsecondsPerQuarter)
            return (error = TempoMapModel::tr("microsecondsPerQuarter must be a decimal string")), false;
    } else if (map.contains(QStringLiteral("bpm"))) {
        const QVariant value = map.value(QStringLiteral("bpm"));
        const QString text = value.metaType().id() == QMetaType::QString
            ? model.parseBpm(value.toString()).value(QStringLiteral("microsecondsPerQuarter")).toString()
            : model.microsecondsForBpm(value.toDouble());
        fields.microsecondsPerQuarter = TimelineTime::parseDecimal(text);
        if (!fields.microsecondsPerQuarter) return (error = TempoMapModel::tr("bpm is not a valid tempo")), false;
    }
    for (const auto &[key, target] : {std::pair{QStringLiteral("numerator"), &fields.numerator},
                                      std::pair{QStringLiteral("denominator"), &fields.denominator}}) {
        if (!map.contains(key)) continue;
        bool ok = false;
        const int value = map.value(key).toInt(&ok);
        if (!ok) return (error = TempoMapModel::tr("%1 must be an integer").arg(key)), false;
        *target = value;
    }
    return true;
}

TempoEventFields merged(TempoEventFields base, const TempoEventFields &input)
{
    if (input.tick) base.tick = input.tick;
    if (input.microsecondsPerQuarter) base.microsecondsPerQuarter = input.microsecondsPerQuarter;
    if (input.numerator) base.numerator = input.numerator;
    if (input.denominator) base.denominator = input.denominator;
    return base;
}

} // namespace

QString TempoMapModel::begin(const TempoEditRequest &request, OperationError *error)
{
    const auto reject = [&](OperationError failure) {
        if (error) *error = failure;
        fail(failure);
        return QString();
    };
    if (m_session) return reject({ErrorCode::Conflict, tr("Another tempo edit is in progress"), {}, {}});
    if (request.expectedRevision && *request.expectedRevision != m_revision)
        return reject({ErrorCode::StaleRevision, tr("The tempo map changed before the edit began"), {}, {}});
    if (!isAdd(request.kind)) {
        const TempoEvent *target = event(request.eventId);
        if (!target) return reject(argument(tr("Unknown tempo event")));
        if (target->tick == 0 && request.kind != TempoEditKind::Edit)
            return reject({ErrorCode::Validation, target->kind == TempoEvent::Kind::Tempo
                               ? tr("The first tempo stays at bar 1") : tr("The first meter stays at bar 1"),
                           QStringLiteral("tick"), {}});
    }
    Session session;
    session.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.request = request;
    if (session.request.label.isEmpty()) session.request.label = defaultLabel(request.kind, event(request.eventId));
    session.baseRevision = m_revision;
    if (isAdd(request.kind)) session.createdId = QUuid::createUuid();
    m_session = session;
    if (error) *error = {};
    emit editStateChanged();
    return m_session->operationId;
}

OperationError TempoMapModel::plan(Session &session, const TempoEventFields &input) const
{
    const auto &request = session.request;
    const TempoEventFields fields = merged(request.fields, input);
    QVector<TempoEvent> events = m_events;
    const auto inEffect = [&](TempoEvent::Kind kind, qint64 tick) {
        const TempoEvent *found = nullptr;
        for (const auto &event : m_events)
            if (event.kind == kind && event.tick <= tick && (!found || event.tick > found->tick)) found = &event;
        return found ? *found : TempoEvent{};
    };
    if (isAdd(request.kind)) {
        if (!fields.tick) return argument(tr("A new tempo event needs a position"));
        const auto kind = request.kind == TempoEditKind::AddTempo ? TempoEvent::Kind::Tempo : TempoEvent::Kind::Meter;
        TempoEvent added = inEffect(kind, *fields.tick);
        added.id = session.createdId;
        added.kind = kind;
        added.tick = *fields.tick;
        if (fields.microsecondsPerQuarter) added.microsecondsPerQuarter = *fields.microsecondsPerQuarter;
        if (fields.numerator) added.numerator = *fields.numerator;
        if (fields.denominator) added.denominator = *fields.denominator;
        events.push_back(added);
    } else {
        qsizetype index = -1;
        for (qsizetype i = 0; i < events.size(); ++i)
            if (events.at(i).id == request.eventId) index = i;
        if (index < 0) return argument(tr("Unknown tempo event"));
        if (request.kind == TempoEditKind::Remove) {
            events.removeAt(index);
        } else {
            TempoEvent &target = events[index];
            if (fields.tick && target.tick == 0 && *fields.tick != 0)
                return {ErrorCode::Validation, tr("The first tempo and meter stay at bar 1"), QStringLiteral("tick"), {}};
            if (fields.tick) target.tick = *fields.tick;
            if (target.kind == TempoEvent::Kind::Tempo && fields.microsecondsPerQuarter)
                target.microsecondsPerQuarter = *fields.microsecondsPerQuarter;
            if (target.kind == TempoEvent::Kind::Meter && fields.numerator) target.numerator = *fields.numerator;
            if (target.kind == TempoEvent::Kind::Meter && fields.denominator) target.denominator = *fields.denominator;
        }
    }
    sortEvents(events);
    session.preview = events;
    return validate(events);
}

OperationError TempoMapModel::update(const QString &operationId, const TempoEventFields &input)
{
    if (!m_session || m_session->operationId != operationId || operationId.isEmpty())
        return argument(tr("No such tempo edit"));
    if (m_session->baseRevision != m_revision)
        return {ErrorCode::StaleRevision, tr("The tempo map changed during the edit"), {}, {}};
    Session &session = *m_session;
    session.error = plan(session, input);
    session.hasPreview = true;
    // A valid candidate is published so the timeline shows its effect live; a
    // placement conflict stays visible but cannot commit. An invalid candidate
    // is never written to the TimeMap.
    if (!session.error) session.error = publish(session.preview);
    else publish(m_events);
    emit eventsChanged();
    return session.error;
}

OperationError TempoMapModel::commit(const QString &operationId, QString *transactionId, QStringList *createdIds)
{
    if (createdIds) createdIds->clear();
    if (!m_session || m_session->operationId != operationId || operationId.isEmpty())
        return fail(argument(tr("No such tempo edit")));
    if (m_session->baseRevision != m_revision) {
        cancel(operationId);
        return fail({ErrorCode::StaleRevision, tr("The tempo map changed during the edit"), {}, {}});
    }
    if (!m_session->hasPreview || m_session->error) {
        const OperationError error = m_session->hasPreview ? m_session->error
                                                           : argument(tr("The tempo edit has no change to commit"));
        cancel(operationId);
        return fail(error);
    }
    const Session session = *m_session;
    TempoChangeSet changes;
    changes.transactionId = operationId;
    changes.label = session.request.label;
    QHash<QUuid, TempoEvent> after;
    for (const auto &event : session.preview) after.insert(event.id, event);
    for (const auto &event : m_events) {
        const auto it = after.constFind(event.id);
        if (it == after.cend()) changes.changes.push_back({event.id, event, std::nullopt});
        else if (!(*it == event)) changes.changes.push_back({event.id, event, *it});
    }
    for (const auto &event : session.preview)
        if (!this->event(event.id)) changes.changes.push_back({event.id, std::nullopt, event});
    m_session.reset();
    emit editStateChanged();
    if (changes.changes.isEmpty()) {
        emit eventsChanged();
        return {};
    }
    m_events = session.preview;
    m_history.resize(m_cursor);
    m_history.push_back(changes);
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = m_history.size();
    ++m_revision;
    if (transactionId) *transactionId = operationId;
    if (createdIds && !session.createdId.isNull()) createdIds->push_back(session.createdId.toString(QUuid::WithoutBraces));
    emit revisionChanged();
    emit historyChanged();
    emit eventsChanged();
    emit committed(operationId, changes.label);
    return {};
}

void TempoMapModel::cancel(const QString &operationId)
{
    if (!m_session || m_session->operationId != operationId) return;
    const bool published = m_session->hasPreview;
    endSession();
    if (published) publish(m_events);
    emit eventsChanged();
}

void TempoMapModel::endSession()
{
    m_session.reset();
    emit editStateChanged();
}

OperationError TempoMapModel::applyChanges(const TempoChangeSet &changes, bool reverse)
{
    QVector<TempoEvent> next = m_events;
    for (const auto &change : changes.changes) {
        const auto &expected = reverse ? change.after : change.before;
        const auto &target = reverse ? change.before : change.after;
        qsizetype index = -1;
        for (qsizetype i = 0; i < next.size(); ++i)
            if (next.at(i).id == change.id) index = i;
        if ((index >= 0) != expected.has_value() || (index >= 0 && !(next.at(index) == *expected)))
            return {ErrorCode::Conflict, tr("The tempo map no longer matches this step"), {}, {}};
        if (target && index >= 0) next[index] = *target;
        else if (target) next.push_back(*target);
        else next.removeAt(index);
    }
    sortEvents(next);
    if (auto error = validate(next)) return error;
    if (auto error = publish(next)) {
        publish(m_events);
        return error;
    }
    m_events = next;
    return {};
}

bool TempoMapModel::undo()
{
    if (m_session) cancel(m_session->operationId);
    if (!canUndo()) return false;
    if (auto error = applyChanges(m_history.at(m_cursor - 1), true)) return !fail(error);
    --m_cursor;
    ++m_revision;
    emit revisionChanged();
    emit historyChanged();
    emit eventsChanged();
    return true;
}

bool TempoMapModel::redo()
{
    if (m_session) cancel(m_session->operationId);
    if (!canRedo()) return false;
    if (auto error = applyChanges(m_history.at(m_cursor), false)) return !fail(error);
    ++m_cursor;
    ++m_revision;
    emit revisionChanged();
    emit historyChanged();
    emit eventsChanged();
    return true;
}

void TempoMapModel::clearHistory()
{
    m_history.clear();
    m_cursor = 0;
    emit historyChanged();
}

bool TempoMapModel::setEvents(const QVector<TempoEvent> &source, OperationError *error)
{
    QVector<TempoEvent> events = source;
    sortEvents(events);
    OperationError failed = validate(events);
    if (!failed) {
        if (m_session) cancel(m_session->operationId);
        failed = publish(events);
        // A beat-locked clip conflict does not block loading a document; the
        // timeline reports it. A TimeMap refusal does.
        if (failed.code == ErrorCode::BackendFailure) publish(m_events);
        else failed = {};
    }
    if (error) *error = failed;
    if (failed) return !fail(failed);
    m_events = events;
    m_history.clear();
    m_cursor = 0;
    ++m_revision;
    emit contentReset();
    emit revisionChanged();
    emit historyChanged();
    emit eventsChanged();
    return true;
}

void TempoMapModel::reset()
{
    setEvents(defaultEvents());
}

QVariantMap TempoMapModel::contentMap() const
{
    QVariantList events;
    for (const auto &event : m_events) {
        QVariantMap map{{QStringLiteral("id"), event.id.toString(QUuid::WithoutBraces)},
                        {QStringLiteral("tick"), QString::number(event.tick)}};
        if (event.kind == TempoEvent::Kind::Tempo) {
            map.insert(QStringLiteral("kind"), QStringLiteral("tempo"));
            map.insert(QStringLiteral("microsecondsPerQuarter"), QString::number(event.microsecondsPerQuarter));
        } else {
            map.insert(QStringLiteral("kind"), QStringLiteral("meter"));
            map.insert(QStringLiteral("numerator"), event.numerator);
            map.insert(QStringLiteral("denominator"), event.denominator);
        }
        events.push_back(map);
    }
    return {{QStringLiteral("schema"), schemaId()}, {QStringLiteral("ticksPerQuarter"), ticksPerQuarter()},
            {QStringLiteral("events"), events}};
}

QVariantMap TempoMapModel::loadContent(const QVariantMap &content)
{
    const auto reject = [this](const QString &message) { return failure(fail(argument(message))); };
    if (content.contains(QStringLiteral("schema")) && content.value(QStringLiteral("schema")).toString() != schemaId())
        return reject(tr("Unsupported tempo map schema"));
    if (content.contains(QStringLiteral("ticksPerQuarter"))
        && content.value(QStringLiteral("ticksPerQuarter")).toInt() != ticksPerQuarter())
        return reject(tr("The tempo map uses %1 ticks per quarter; the time map uses %2")
                          .arg(content.value(QStringLiteral("ticksPerQuarter")).toInt()).arg(ticksPerQuarter()));
    QVector<TempoEvent> events;
    for (const auto &value : content.value(QStringLiteral("events")).toList()) {
        const QVariantMap map = value.toMap();
        TempoEvent event;
        event.id = QUuid(map.value(QStringLiteral("id")).toString());
        const QString kind = map.value(QStringLiteral("kind")).toString();
        if (event.id.isNull() || (kind != QLatin1String("tempo") && kind != QLatin1String("meter")))
            return reject(tr("A tempo event needs a UUID id and kind \"tempo\" or \"meter\""));
        event.kind = kind == QLatin1String("tempo") ? TempoEvent::Kind::Tempo : TempoEvent::Kind::Meter;
        TempoEventFields fields;
        QString error;
        if (!readFields(*this, map, fields, error)) return reject(error);
        if (!fields.tick) return reject(tr("A tempo event needs a tick"));
        event.tick = *fields.tick;
        if (event.kind == TempoEvent::Kind::Tempo) {
            if (!fields.microsecondsPerQuarter) return reject(tr("A tempo event needs microsecondsPerQuarter"));
            event.microsecondsPerQuarter = *fields.microsecondsPerQuarter;
        } else {
            if (!fields.numerator || !fields.denominator) return reject(tr("A meter event needs numerator and denominator"));
            event.numerator = *fields.numerator;
            event.denominator = *fields.denominator;
        }
        events.push_back(event);
    }
    OperationError error;
    if (!setEvents(events, &error)) return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("revision"), revisionText()}};
}

QString TempoMapModel::toJson() const
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(contentMap())).toJson(QJsonDocument::Indented));
}

QVariantMap TempoMapModel::loadJson(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return failure(fail(argument(tr("The tempo map is not a JSON object"))));
    return loadContent(document.object().toVariantMap());
}

// ---- QML transaction facade ----

QVariantMap TempoMapModel::beginEdit(const QVariantMap &map)
{
    static const QHash<QString, TempoEditKind> kinds{
        {QStringLiteral("addTempo"), TempoEditKind::AddTempo}, {QStringLiteral("addMeter"), TempoEditKind::AddMeter},
        {QStringLiteral("move"), TempoEditKind::Move}, {QStringLiteral("edit"), TempoEditKind::Edit},
        {QStringLiteral("remove"), TempoEditKind::Remove}};
    const auto kind = kinds.constFind(map.value(QStringLiteral("kind")).toString());
    if (kind == kinds.cend()) return failure(fail(argument(tr("Unknown tempo edit kind"))));
    TempoEditRequest request;
    request.kind = *kind;
    request.eventId = QUuid(map.value(QStringLiteral("eventId")).toString());
    request.label = map.value(QStringLiteral("label")).toString();
    if (map.contains(QStringLiteral("expectedRevision"))) {
        bool ok = false;
        request.expectedRevision = map.value(QStringLiteral("expectedRevision")).toString().toULongLong(&ok);
        if (!ok) return failure(fail(argument(tr("expectedRevision must be a decimal string"))));
    }
    QString error;
    if (!readFields(*this, map, request.fields, error)) return failure(fail(argument(error)));
    OperationError failed;
    const QString operation = begin(request, &failed);
    if (operation.isEmpty()) return failure(failed);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("operationId"), operation}};
}

QVariantMap TempoMapModel::updateEdit(const QString &operationId, const QVariantMap &input)
{
    TempoEventFields fields;
    QString error;
    if (!readFields(*this, input, fields, error)) return failure(argument(error));
    if (!m_session || m_session->operationId != operationId) return failure(argument(tr("No such tempo edit")));
    const OperationError result = update(operationId, fields);
    // A soft error keeps the session: the preview is drawn invalid.
    return {{QStringLiteral("ok"), result.code != ErrorCode::StaleRevision && result.code != ErrorCode::InvalidArgument},
            {QStringLiteral("valid"), !result}, {QStringLiteral("error"), result.message}};
}

QVariantMap TempoMapModel::commitEdit(const QString &operationId)
{
    QString transaction;
    QStringList created;
    if (auto error = commit(operationId, &transaction, &created)) return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transaction},
            {QStringLiteral("changed"), !transaction.isEmpty()}, {QStringLiteral("createdIds"), created},
            {QStringLiteral("revision"), revisionText()}};
}

bool TempoMapModel::cancelEdit(const QString &operationId)
{
    if (!m_session || m_session->operationId != operationId) return false;
    cancel(operationId);
    return true;
}

QVariantMap TempoMapModel::applyEdit(const QVariantMap &request, const QVariantMap &input)
{
    const QVariantMap begun = beginEdit(request);
    if (!begun.value(QStringLiteral("ok")).toBool()) return begun;
    const QString operation = begun.value(QStringLiteral("operationId")).toString();
    const QVariantMap updated = updateEdit(operation, input);
    if (!updated.value(QStringLiteral("valid")).toBool()) {
        cancel(operation);
        return failure(fail({ErrorCode::Validation, updated.value(QStringLiteral("error")).toString(), {}, {}}));
    }
    return commitEdit(operation);
}

} // namespace QindaTK
