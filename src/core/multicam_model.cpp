// SPDX-License-Identifier: LGPL-3.0-or-later
#include "multicam_model.h"

#include "media_source_info.h"
#include "timeline_planner.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <limits>

namespace QindaTK {
namespace {
const QString kSchema = QStringLiteral("qindatk.multicam/1");

QString tr(const char *source)
{
    return QCoreApplication::translate("MulticamModel", source);
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

std::optional<qint64> decimal(const QVariant &value)
{
    return value.metaType().id() == QMetaType::QString ? TimelineTime::parseDecimal(value.toString())
                                                        : std::nullopt;
}
} // namespace

MulticamModel::MulticamModel(QObject *parent) : QObject(parent) {}

void MulticamModel::setTimeline(TimelineModel *timeline)
{
    if (m_timeline == timeline) return;
    disconnect(m_timelineLink);
    m_timeline = timeline;
    if (timeline)
        m_timelineLink = connect(timeline, &TimelineModel::revisionChanged, this, &MulticamModel::anglesChanged);
    emit bindingChanged();
    emit anglesChanged();
}

void MulticamModel::setResolver(ResourceResolver *resolver)
{
    if (m_resolver == resolver) return;
    m_resolver = resolver;
    emit bindingChanged();
    emit anglesChanged();
}

void MulticamModel::setPlayer(MediaPlayer *player)
{
    if (m_player == player) return;
    disconnect(m_playerLink);
    m_player = player;
    // AGENT-NOTE: with a player the playhead is the frame on screen, so a
    // switch made while it plays cuts at the frame the user saw.
    if (player) m_playerLink = connect(player, &MediaPlayer::frameChanged, this, &MulticamModel::followPlayer);
    emit bindingChanged();
    followPlayer();
}

void MulticamModel::followPlayer()
{
    if (!m_player) return;
    const QString ticks = m_player->presentedTicks();
    if (!ticks.isEmpty()) setPlayheadTicks(ticks);
}

void MulticamModel::setTrackId(const QString &trackId)
{
    if (m_trackId == trackId) return;
    m_trackId = trackId;
    emit settingsChanged();
    emit anglesChanged();
}

void MulticamModel::setAudioTrackId(const QString &trackId)
{
    if (m_audioTrackId == trackId) return;
    m_audioTrackId = trackId;
    emit settingsChanged();
}

void MulticamModel::setSyncStartTicks(const QString &ticks)
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!value) {
        fail(tr("syncStartTicks must be an exact decimal tick count"));
        return;
    }
    if (m_syncStart == *value) return;
    m_syncStart = *value;
    emit settingsChanged();
    emit anglesChanged();
}

void MulticamModel::setAudioFollowsVideo(bool follows)
{
    if (m_audioFollows == follows) return;
    m_audioFollows = follows;
    emit settingsChanged();
}

void MulticamModel::setPlayheadTicks(const QString &ticks)
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!value || *value == m_playhead) return;
    m_playhead = *value;
    emit anglesChanged();
}

QVariantMap MulticamModel::refuse(const QString &message)
{
    fail(message);
    return failure(message);
}

bool MulticamModel::fail(const QString &message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit lastErrorChanged();
    }
    return false;
}

const MulticamModel::Angle *MulticamModel::angle(const QString &id) const
{
    for (const Angle &item : m_angles)
        if (item.id == id) return &item;
    return nullptr;
}

const MulticamModel::Angle *MulticamModel::angleForResource(const QString &resourceId) const
{
    for (const Angle &item : m_angles)
        if (item.resourceId == resourceId) return &item;
    return nullptr;
}

std::optional<qint64> MulticamModel::sourceAt(const Angle &item, qint64 ticks) const
{
    const auto sync = TimelineTime::subtract(ticks, m_syncStart);
    return sync ? TimelineTime::add(*sync, item.syncOffset) : std::nullopt;
}

const TimelineClip *MulticamModel::programClipAt(qint64 ticks) const
{
    if (!m_timeline || ticks == std::numeric_limits<qint64>::max()) return nullptr;
    const QVector<QUuid> hits = m_timeline->clipsInRange(QUuid(m_trackId), ticks, ticks + 1);
    return hits.isEmpty() ? nullptr : m_timeline->clipRecord(hits.last());
}

bool MulticamModel::available(const Angle &item) const
{
    if (m_resolver) {
        const QVariantMap entry = m_resolver->lookup(item.resourceId);
        return !entry.isEmpty() && entry.value(QStringLiteral("availability")).toString() != QLatin1String("missing");
    }
    const QString path = mediaLocalPath(item.resourceId);
    return !path.isEmpty() && QFileInfo::exists(path);
}

QString MulticamModel::activeAngleId() const
{
    const TimelineClip *clip = programClipAt(m_playhead);
    const Angle *item = clip ? angleForResource(clip->resourceId) : nullptr;
    return item ? item->id : QString();
}

QString MulticamModel::frameRateNumerator() const
{
    return m_timeline ? QString::number(m_timeline->timeMap()->frameRateNumerator()) : QStringLiteral("25");
}

QString MulticamModel::frameRateDenominator() const
{
    return m_timeline ? QString::number(m_timeline->timeMap()->frameRateDenominator()) : QStringLiteral("1");
}

QVariantList MulticamModel::angles() const
{
    const QString active = activeAngleId();
    QVariantList result;
    for (const Angle &item : m_angles) {
        const auto source = sourceAt(item, m_playhead);
        const bool inside = source && *source >= 0 && (item.sourceDuration < 0 || *source < item.sourceDuration);
        result.push_back(QVariantMap{
            {QStringLiteral("angleId"), item.id},
            {QStringLiteral("label"), item.label.isEmpty() ? item.id : item.label},
            {QStringLiteral("resourceId"), item.resourceId},
            {QStringLiteral("audioResourceId"), item.audioResourceId},
            {QStringLiteral("syncOffsetTicks"), QString::number(item.syncOffset)},
            {QStringLiteral("sourceDurationTicks"), QString::number(item.sourceDuration)},
            // The source time each angle shows at the playhead (thumbnail requests).
            {QStringLiteral("frameTimeTicks"), inside ? QString::number(*source) : QString()},
            {QStringLiteral("frameId"), QString()},
            {QStringLiteral("hasMedia"), inside},
            {QStringLiteral("available"), inside && available(item)},
            {QStringLiteral("active"), item.id == active}});
    }
    return result;
}

bool MulticamModel::addAngle(const QVariantMap &map)
{
    Angle item;
    item.id = map.value(QStringLiteral("angleId")).toString();
    item.label = map.value(QStringLiteral("label")).toString();
    item.resourceId = map.value(QStringLiteral("resourceId")).toString();
    item.audioResourceId = map.value(QStringLiteral("audioResourceId")).toString();
    const auto offset = decimal(map.value(QStringLiteral("syncOffsetTicks"), QStringLiteral("0")));
    const auto duration = decimal(map.value(QStringLiteral("sourceDurationTicks"), QStringLiteral("-1")));
    if (item.id.isEmpty() || item.resourceId.isEmpty())
        return fail(tr("An angle needs an ID and a resource"));
    if (!offset || !duration || *duration < -1)
        return fail(tr("Angle offsets and durations are exact decimal tick strings"));
    if (angle(item.id) || angleForResource(item.resourceId))
        return fail(tr("Angle IDs and resources must be unique"));
    if (m_angles.size() >= kMaxAngles) return fail(tr("A multicam group has at most 64 angles"));
    item.syncOffset = *offset;
    item.sourceDuration = *duration;
    m_angles.push_back(item);
    emit anglesChanged();
    return true;
}

bool MulticamModel::removeAngle(const QString &angleId)
{
    for (qsizetype i = 0; i < m_angles.size(); ++i) {
        if (m_angles.at(i).id != angleId) continue;
        m_angles.removeAt(i);
        emit anglesChanged();
        return true;
    }
    return fail(tr("Unknown angle %1").arg(angleId));
}

bool MulticamModel::setSyncOffset(const QString &angleId, const QString &syncOffsetTicks)
{
    const auto offset = TimelineTime::parseDecimal(syncOffsetTicks);
    for (Angle &item : m_angles) {
        if (item.id != angleId) continue;
        if (!offset) return fail(tr("syncOffsetTicks must be an exact decimal tick count"));
        item.syncOffset = *offset;
        emit anglesChanged();
        return true;
    }
    return fail(tr("Unknown angle %1").arg(angleId));
}

QString MulticamModel::sourceTicksAt(const QString &angleId, const QString &ticks) const
{
    const Angle *item = angle(angleId);
    const auto at = TimelineTime::parseDecimal(ticks);
    const auto source = item && at ? sourceAt(*item, *at) : std::nullopt;
    return source ? QString::number(*source) : QString();
}

QString MulticamModel::angleAt(const QString &ticks) const
{
    const auto at = TimelineTime::parseDecimal(ticks);
    const TimelineClip *clip = at ? programClipAt(*at) : nullptr;
    const Angle *item = clip ? angleForResource(clip->resourceId) : nullptr;
    return item ? item->id : QString();
}

QVariantMap MulticamModel::insertProgram(const QString &angleId, const QString &startTicks,
                                         const QString &durationTicks)
{
    const Angle *item = angle(angleId);
    const auto start = TimelineTime::parseDecimal(startTicks);
    const auto duration = TimelineTime::parseDecimal(durationTicks);
    if (!m_timeline || !item || !start || !duration || *duration < 1)
        return refuse(tr("insertProgram needs a timeline, a known angle and exact ticks"));
    const auto source = sourceAt(*item, *start);
    if (!source) return refuse(tr("The sync position is out of range"));
    TimelineEditRequest request;
    request.kind = TimelineEditKind::InsertClips;
    request.trackId = QUuid(m_trackId);
    request.label = tr("Insert multicam program");
    const QUuid link = m_audioTrackId.isEmpty() ? QUuid() : QUuid::createUuid();
    TimelineClipTemplate picture;
    picture.clip.id = QUuid::createUuid();
    picture.clip.duration = *duration;
    picture.clip.sourceOffset = *source;
    picture.clip.sourceDuration = item->sourceDuration;
    picture.clip.resourceId = item->resourceId;
    picture.clip.kind = QStringLiteral("video");
    picture.clip.label = item->label.isEmpty() ? item->id : item->label;
    picture.clip.linkId = link;
    request.templates.push_back(picture);
    if (!m_audioTrackId.isEmpty()) {
        // Templates are placed relative to trackId in visible order.
        const QVector<QUuid> order = m_timeline->visibleTrackOrder();
        const qsizetype from = order.indexOf(QUuid(m_trackId));
        const qsizetype to = order.indexOf(QUuid(m_audioTrackId));
        if (from < 0 || to <= from)
            return refuse(tr("The audio track must be a visible track below the program track"));
        TimelineClipTemplate sound = picture;
        sound.clip.id = QUuid::createUuid();
        sound.clip.kind = QStringLiteral("audio");
        if (!item->audioResourceId.isEmpty()) sound.clip.resourceId = item->audioResourceId;
        sound.trackOffset = int(to - from);
        request.templates.push_back(sound);
    }
    TimelineEditInput input;
    input.pointerTicks = *start;
    input.snap = false;
    QString transaction;
    OperationError error;
    const QString operation = m_timeline->begin(request, &error);
    if (operation.isEmpty()) return refuse(error.message);
    const TimelinePlan plan = m_timeline->update(operation, input);
    if (plan.error) {
        m_timeline->cancel(operation);
        return refuse(plan.error.message);
    }
    QStringList created;
    if (const OperationError committed = m_timeline->commit(operation, &transaction, &created))
        return refuse(committed.message);
    fail({});
    emit anglesChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("clipIds"), created},
            {QStringLiteral("transactionId"), transaction}};
}

// AGENT-NOTE (D-121): the new source position comes from the sync map, not
// from the clip being cut, so a switch always lands in sync even after the
// program clip was slipped; the TimelineModel planner enforces the angle's
// media bounds and makes the cut frame-exact.
QVariantMap MulticamModel::switchAngle(const QString &angleId, const QString &atTicks)
{
    const Angle *item = angle(angleId);
    if (!m_timeline || !item) return refuse(tr("Unknown angle %1").arg(angleId));
    const auto requested = atTicks.isEmpty() ? std::optional<qint64>(m_playhead) : TimelineTime::parseDecimal(atTicks);
    const auto at = requested ? TimelinePlanner::framePoint(*m_timeline, *requested) : std::nullopt;
    if (!at) return refuse(tr("The switch time is out of range"));
    const TimelineClip *clip = programClipAt(*at);
    if (!clip) return refuse(tr("No program clip plays at the switch time"));
    if (clip->resourceId == item->resourceId) return refuse(tr("Angle %1 is already on program").arg(angleId));
    const auto source = sourceAt(*item, *at);
    if (!source) return refuse(tr("The sync position is out of range"));
    TimelineEditRequest request;
    request.kind = TimelineEditKind::SwitchSource;
    request.clipIds = {clip->id};
    request.linked = m_audioFollows;
    request.frameExact = true;
    request.resourceId = item->resourceId;
    request.linkedResourceId = item->audioResourceId;
    request.sourceOffset = *source;
    request.sourceDuration = item->sourceDuration;
    request.label = tr("Switch to angle %1").arg(item->label.isEmpty() ? item->id : item->label);
    TimelineEditInput input;
    input.pointerTicks = *at;
    input.snap = false;
    QString transaction;
    if (const OperationError error = m_timeline->apply(request, input, &transaction))
        return refuse(error.message);
    fail({});
    emit anglesChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transaction},
            {QStringLiteral("atTicks"), QString::number(*at)}, {QStringLiteral("activeAngleId"), angleAt(QString::number(*at))}};
}

QStringList MulticamModel::unsyncedClipIds() const
{
    QStringList result;
    if (!m_timeline) return result;
    for (const auto &entry : m_timeline->trackIndex(QUuid(m_trackId))) {
        const TimelineClip *clip = m_timeline->clipRecord(entry.id);
        const Angle *item = clip ? angleForResource(clip->resourceId) : nullptr;
        if (!item) continue;
        const auto expected = sourceAt(*item, clip->start);
        if (!expected || *expected != clip->sourceOffset) result.push_back(clip->id.toString(QUuid::WithoutBraces));
    }
    return result;
}

QVariantMap MulticamModel::contentMap() const
{
    QVariantList list;
    for (const Angle &item : m_angles)
        list.push_back(QVariantMap{{QStringLiteral("angleId"), item.id}, {QStringLiteral("label"), item.label},
                                   {QStringLiteral("resourceId"), item.resourceId},
                                   {QStringLiteral("audioResourceId"), item.audioResourceId},
                                   {QStringLiteral("syncOffsetTicks"), QString::number(item.syncOffset)},
                                   {QStringLiteral("sourceDurationTicks"), QString::number(item.sourceDuration)}});
    return {{QStringLiteral("schema"), kSchema}, {QStringLiteral("trackId"), m_trackId},
            {QStringLiteral("audioTrackId"), m_audioTrackId}, {QStringLiteral("syncStartTicks"), syncStartTicks()},
            {QStringLiteral("audioFollowsVideo"), m_audioFollows}, {QStringLiteral("angles"), list}};
}

bool MulticamModel::loadContent(const QVariantMap &content)
{
    if (content.value(QStringLiteral("schema")).toString() != kSchema) return fail(tr("Unsupported multicam schema"));
    const auto sync = decimal(content.value(QStringLiteral("syncStartTicks")));
    if (!sync) return fail(tr("syncStartTicks must be an exact decimal tick count"));
    const QVector<Angle> previous = m_angles;
    m_angles.clear();
    for (const QVariant &value : content.value(QStringLiteral("angles")).toList()) {
        if (!addAngle(value.toMap())) {
            m_angles = previous;
            emit anglesChanged();
            return false;
        }
    }
    m_trackId = content.value(QStringLiteral("trackId")).toString();
    m_audioTrackId = content.value(QStringLiteral("audioTrackId")).toString();
    m_syncStart = *sync;
    m_audioFollows = content.value(QStringLiteral("audioFollowsVideo"), true).toBool();
    fail({});
    emit settingsChanged();
    emit anglesChanged();
    return true;
}

} // namespace QindaTK
