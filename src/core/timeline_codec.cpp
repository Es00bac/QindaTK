// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_codec.h"

#include <QCoreApplication>

namespace QindaTK {
namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("TimelineModel", text);
}

bool setError(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool readTicks(const QVariantMap &map, const QString &key, qint64 &target, QString *error,
               bool required)
{
    if (!map.contains(key)) return !required || setError(error, tr("Missing %1").arg(key));
    const auto value = TimelineCodec::ticks(map.value(key));
    if (!value) return setError(error, tr("%1 must be an exact decimal string").arg(key));
    target = *value;
    return true;
}

bool readBool(const QVariantMap &map, const QString &key, bool &target, QString *error)
{
    if (!map.contains(key)) return true;
    const QVariant value = map.value(key);
    if (value.metaType().id() != QMetaType::Bool) return setError(error, tr("%1 must be a boolean").arg(key));
    target = value.toBool();
    return true;
}

bool readString(const QVariantMap &map, const QString &key, QString &target, QString *error)
{
    if (!map.contains(key)) return true;
    const QVariant value = map.value(key);
    if (value.isNull()) {
        target.clear();
        return true;
    }
    if (value.metaType().id() != QMetaType::QString) return setError(error, tr("%1 must be a string").arg(key));
    target = value.toString();
    return true;
}

bool readId(const QVariantMap &map, const QString &key, QUuid &target, QString *error, bool required)
{
    if (!map.contains(key)) return !required || setError(error, tr("Missing %1").arg(key));
    const QUuid id = TimelineCodec::uuid(map.value(key));
    if (id.isNull()) return setError(error, tr("%1 must be a UUID string").arg(key));
    target = id;
    return true;
}
} // namespace

namespace TimelineCodec {

std::optional<qint64> ticks(const QVariant &value)
{
    if (value.metaType().id() != QMetaType::QString) return std::nullopt;
    return TimelineTime::parseDecimal(value.toString());
}

QUuid uuid(const QVariant &value)
{
    if (value.metaType().id() != QMetaType::QString) return {};
    return QUuid(value.toString());
}

QVariantMap toMap(const TimelineTrack &track)
{
    return {{QStringLiteral("id"), track.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("name"), track.name},
            {QStringLiteral("kind"), track.kind},
            {QStringLiteral("height"), track.height},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("solo"), track.solo},
            {QStringLiteral("armed"), track.armed},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("hidden"), track.hidden},
            {QStringLiteral("routing"), track.routing},
            {QStringLiteral("color"), track.color}};
}

QVariantMap toMap(const TimelineClip &clip)
{
    return {{QStringLiteral("id"), clip.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("trackId"), clip.trackId.toString(QUuid::WithoutBraces)},
            {QStringLiteral("start"), QString::number(clip.start)},
            {QStringLiteral("duration"), QString::number(clip.duration)},
            {QStringLiteral("end"), QString::number(clip.end())},
            {QStringLiteral("sourceOffset"), QString::number(clip.sourceOffset)},
            {QStringLiteral("sourceDuration"), QString::number(clip.sourceDuration)},
            {QStringLiteral("resourceId"), clip.resourceId},
            {QStringLiteral("label"), clip.label},
            {QStringLiteral("color"), clip.color},
            {QStringLiteral("kind"), clip.kind},
            {QStringLiteral("locked"), clip.locked}};
}

QVariantMap toMap(const TimelineMarker &marker)
{
    return {{QStringLiteral("id"), marker.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("position"), QString::number(marker.position)},
            {QStringLiteral("duration"), QString::number(marker.duration)},
            {QStringLiteral("anchor"), marker.anchor == TimelineMarker::Anchor::Musical
                                           ? QStringLiteral("musical") : QStringLiteral("time")},
            {QStringLiteral("musicalTick"), QString::number(marker.musicalTick)},
            {QStringLiteral("label"), marker.label},
            {QStringLiteral("color"), marker.color},
            {QStringLiteral("kind"), marker.kind},
            {QStringLiteral("locked"), marker.locked}};
}

std::optional<TimelineTrack> trackFromMap(const QVariantMap &map, QString *error,
                                          const TimelineTrack *base)
{
    TimelineTrack track = base ? *base : TimelineTrack{};
    if (!readId(map, QStringLiteral("id"), track.id, error, !base)) return std::nullopt;
    if (!readString(map, QStringLiteral("name"), track.name, error)
        || !readString(map, QStringLiteral("kind"), track.kind, error)
        || !readString(map, QStringLiteral("routing"), track.routing, error)
        || !readString(map, QStringLiteral("color"), track.color, error)
        || !readBool(map, QStringLiteral("muted"), track.muted, error)
        || !readBool(map, QStringLiteral("solo"), track.solo, error)
        || !readBool(map, QStringLiteral("armed"), track.armed, error)
        || !readBool(map, QStringLiteral("locked"), track.locked, error)
        || !readBool(map, QStringLiteral("hidden"), track.hidden, error))
        return std::nullopt;
    if (map.contains(QStringLiteral("height"))) {
        bool ok = false;
        const int height = map.value(QStringLiteral("height")).toInt(&ok);
        if (!ok || height < 0 || height > 4096) {
            setError(error, tr("height must be an integer from 0 to 4096"));
            return std::nullopt;
        }
        track.height = height;
    }
    return track;
}

std::optional<TimelineClip> clipFromMap(const QVariantMap &map, QString *error,
                                        const TimelineClip *base)
{
    TimelineClip clip = base ? *base : TimelineClip{};
    const bool required = !base;
    if (!readId(map, QStringLiteral("id"), clip.id, error, required)
        || !readId(map, QStringLiteral("trackId"), clip.trackId, error, required)
        || !readTicks(map, QStringLiteral("start"), clip.start, error, required)
        || !readTicks(map, QStringLiteral("duration"), clip.duration, error, required)
        || !readTicks(map, QStringLiteral("sourceOffset"), clip.sourceOffset, error, false)
        || !readTicks(map, QStringLiteral("sourceDuration"), clip.sourceDuration, error, false)
        || !readString(map, QStringLiteral("resourceId"), clip.resourceId, error)
        || !readString(map, QStringLiteral("label"), clip.label, error)
        || !readString(map, QStringLiteral("color"), clip.color, error)
        || !readString(map, QStringLiteral("kind"), clip.kind, error)
        || !readBool(map, QStringLiteral("locked"), clip.locked, error))
        return std::nullopt;
    if (clip.duration < 1) {
        setError(error, tr("duration must be at least one tick"));
        return std::nullopt;
    }
    if (!TimelineTime::add(clip.start, clip.duration)) {
        setError(error, tr("clip end is out of range"));
        return std::nullopt;
    }
    if (clip.sourceDuration < -1) {
        setError(error, tr("sourceDuration must be -1 or a length"));
        return std::nullopt;
    }
    return clip;
}

std::optional<TimelineMarker> markerFromMap(const QVariantMap &map, QString *error,
                                            const TimelineMarker *base)
{
    TimelineMarker marker = base ? *base : TimelineMarker{};
    if (!readId(map, QStringLiteral("id"), marker.id, error, false)
        || !readTicks(map, QStringLiteral("position"), marker.position, error, !base)
        || !readTicks(map, QStringLiteral("duration"), marker.duration, error, false)
        || !readTicks(map, QStringLiteral("musicalTick"), marker.musicalTick, error, false)
        || !readString(map, QStringLiteral("label"), marker.label, error)
        || !readString(map, QStringLiteral("color"), marker.color, error)
        || !readString(map, QStringLiteral("kind"), marker.kind, error)
        || !readBool(map, QStringLiteral("locked"), marker.locked, error))
        return std::nullopt;
    if (map.contains(QStringLiteral("anchor"))) {
        const QString anchor = map.value(QStringLiteral("anchor")).toString();
        if (anchor == QLatin1String("time")) marker.anchor = TimelineMarker::Anchor::Time;
        else if (anchor == QLatin1String("musical")) marker.anchor = TimelineMarker::Anchor::Musical;
        else {
            setError(error, tr("anchor must be \"time\" or \"musical\""));
            return std::nullopt;
        }
    }
    if (marker.duration < 0 || !TimelineTime::add(marker.position, marker.duration)) {
        setError(error, tr("marker duration must be a non-negative length in range"));
        return std::nullopt;
    }
    return marker;
}

QVariantMap changeToMap(const TimelineChange &change)
{
    QVariantMap map;
    switch (change.kind) {
    case TimelineChange::Kind::Track:
        map.insert(QStringLiteral("kind"), QStringLiteral("track"));
        map.insert(QStringLiteral("before"), change.trackBefore ? QVariant(toMap(*change.trackBefore)) : QVariant());
        map.insert(QStringLiteral("after"), change.trackAfter ? QVariant(toMap(*change.trackAfter)) : QVariant());
        break;
    case TimelineChange::Kind::Clip:
        map.insert(QStringLiteral("kind"), QStringLiteral("clip"));
        map.insert(QStringLiteral("before"), change.clipBefore ? QVariant(toMap(*change.clipBefore)) : QVariant());
        map.insert(QStringLiteral("after"), change.clipAfter ? QVariant(toMap(*change.clipAfter)) : QVariant());
        break;
    case TimelineChange::Kind::Marker:
        map.insert(QStringLiteral("kind"), QStringLiteral("marker"));
        map.insert(QStringLiteral("before"), change.markerBefore ? QVariant(toMap(*change.markerBefore)) : QVariant());
        map.insert(QStringLiteral("after"), change.markerAfter ? QVariant(toMap(*change.markerAfter)) : QVariant());
        break;
    case TimelineChange::Kind::TrackOrder: {
        map.insert(QStringLiteral("kind"), QStringLiteral("trackOrder"));
        QStringList before, after;
        for (const auto &id : change.orderBefore) before.push_back(id.toString(QUuid::WithoutBraces));
        for (const auto &id : change.orderAfter) after.push_back(id.toString(QUuid::WithoutBraces));
        map.insert(QStringLiteral("orderBefore"), before);
        map.insert(QStringLiteral("orderAfter"), after);
        break;
    }
    }
    if (!change.id.isNull()) map.insert(QStringLiteral("id"), change.id.toString(QUuid::WithoutBraces));
    return map;
}

std::optional<TimelineChange> changeFromMap(const QVariantMap &map, QString *error)
{
    TimelineChange change;
    const QString kind = map.value(QStringLiteral("kind")).toString();
    const QVariant before = map.value(QStringLiteral("before"));
    const QVariant after = map.value(QStringLiteral("after"));
    const auto side = [&](const QVariant &value, auto reader, auto &target) {
        if (value.isNull() || !value.isValid()) return true;
        const auto record = reader(value.toMap(), error, nullptr);
        if (!record) return false;
        target = *record;
        return true;
    };
    if (kind == QLatin1String("clip")) {
        change.kind = TimelineChange::Kind::Clip;
        if (!side(before, clipFromMap, change.clipBefore) || !side(after, clipFromMap, change.clipAfter))
            return std::nullopt;
    } else if (kind == QLatin1String("track")) {
        change.kind = TimelineChange::Kind::Track;
        if (!side(before, trackFromMap, change.trackBefore) || !side(after, trackFromMap, change.trackAfter))
            return std::nullopt;
    } else if (kind == QLatin1String("marker")) {
        change.kind = TimelineChange::Kind::Marker;
        if (!side(before, markerFromMap, change.markerBefore) || !side(after, markerFromMap, change.markerAfter))
            return std::nullopt;
    } else if (kind == QLatin1String("trackOrder")) {
        change.kind = TimelineChange::Kind::TrackOrder;
        for (const auto &id : map.value(QStringLiteral("orderBefore")).toStringList())
            change.orderBefore.push_back(QUuid(id));
        for (const auto &id : map.value(QStringLiteral("orderAfter")).toStringList())
            change.orderAfter.push_back(QUuid(id));
        return change;
    } else {
        setError(error, tr("Unknown change kind"));
        return std::nullopt;
    }
    change.id = uuid(map.value(QStringLiteral("id")));
    return change;
}

} // namespace TimelineCodec
} // namespace QindaTK
