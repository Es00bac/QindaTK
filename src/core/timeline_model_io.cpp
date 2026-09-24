// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_codec.h"
#include "timeline_model.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace QindaTK {
namespace {
QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool reject(OperationError *error, const QString &message)
{
    if (error) *error = {ErrorCode::InvalidArgument, message, {}, {}};
    return false;
}
} // namespace

bool TimelineModel::setContent(TimelineRate timebase, const QVector<TimelineTrack> &tracks,
                               const QVector<TimelineClip> &clips,
                               const QVector<TimelineMarker> &markers, OperationError *error,
                               const QUuid &documentId)
{
    if (!timebase.isValid()) return reject(error, tr("The timebase must be a positive rational"));
    if (tracks.size() > kMaxTracks || clips.size() > kMaxClips || markers.size() > kMaxMarkers)
        return reject(error, tr("The timeline exceeds its track, clip or marker limit"));
    QSet<QUuid> ids;
    for (const auto &track : tracks) {
        if (track.id.isNull() || ids.contains(track.id))
            return reject(error, tr("Track IDs must be unique UUIDs"));
        if (track.height < 0 || track.height > 4096)
            return reject(error, tr("Track height must be 0 to 4096"));
        ids.insert(track.id);
    }
    const QSet<QUuid> trackIds = ids;
    for (const auto &clip : clips) {
        if (clip.id.isNull() || ids.contains(clip.id))
            return reject(error, tr("Clip IDs must be unique UUIDs"));
        if (!trackIds.contains(clip.trackId))
            return reject(error, tr("Clip %1 names a missing track").arg(clip.id.toString(QUuid::WithoutBraces)));
        if (clip.duration < 1 || !TimelineTime::add(clip.start, clip.duration) || clip.sourceDuration < -1)
            return reject(error, tr("Clip %1 has an invalid interval").arg(clip.id.toString(QUuid::WithoutBraces)));
        ids.insert(clip.id);
    }
    for (const auto &marker : markers) {
        if (marker.id.isNull() || ids.contains(marker.id))
            return reject(error, tr("Marker IDs must be unique UUIDs"));
        if (marker.duration < 0 || !TimelineTime::add(marker.position, marker.duration))
            return reject(error, tr("Marker %1 has an invalid range").arg(marker.id.toString(QUuid::WithoutBraces)));
        ids.insert(marker.id);
    }
    if (m_session) {
        invalidateSession(tr("The timeline was reloaded"));
        endSession();
    }
    m_timebase = timebase;
    m_trackOrder.clear();
    m_tracks.clear();
    m_clips.clear();
    m_markers.clear();
    m_trackOrder.reserve(tracks.size());
    for (const auto &track : tracks) {
        m_trackOrder.push_back(track.id);
        m_tracks.insert(track.id, track);
    }
    m_clips.reserve(clips.size());
    for (const auto &clip : clips) m_clips.insert(clip.id, withDerivedStart(clip));
    for (const auto &marker : markers) m_markers.insert(marker.id, withDerivedPosition(marker));
    if (!documentId.isNull()) m_documentId = DocumentId{documentId};
    m_hasRange = false;
    m_rangeStart = m_rangeEnd = 0;
    rebuildAllIndexes();
    m_history.clear();
    m_cursor = 0;
    m_historyBytes = 0;
    m_externalSteps.clear();
    m_externalOrder.clear();
    ++m_generation;
    ++m_revision;
    if (error) *error = {};
    emit contentReset();
    emit structureChanged();
    emit clipsChanged();
    emit markersChanged();
    emit editRangeChanged();
    emit historyChanged();
    emit revisionChanged();
    return true;
}

QVariantMap TimelineModel::loadContent(const QVariantMap &content)
{
    TimelineRate timebase = m_timebase;
    if (content.contains(QStringLiteral("timebase"))) {
        const QVariantMap rate = content.value(QStringLiteral("timebase")).toMap();
        const auto numerator = TimelineCodec::ticks(rate.value(QStringLiteral("numerator")));
        const auto denominator = TimelineCodec::ticks(rate.value(QStringLiteral("denominator")));
        const auto normalized = numerator && denominator
                                    ? TimelineRate::normalized(*numerator, *denominator) : std::nullopt;
        if (!normalized) return failure(tr("timebase needs decimal-string numerator and denominator"));
        timebase = *normalized;
    }
    QString error;
    QVector<TimelineTrack> tracks;
    for (const auto &value : content.value(QStringLiteral("tracks")).toList()) {
        const auto track = TimelineCodec::trackFromMap(value.toMap(), &error);
        if (!track) return failure(tr("Track: %1").arg(error));
        tracks.push_back(*track);
    }
    QVector<TimelineClip> clips;
    const QVariantList clipList = content.value(QStringLiteral("clips")).toList();
    clips.reserve(clipList.size());
    QVector<qsizetype> unanchored; // beat-locked clips saved without a musical anchor
    for (const auto &value : clipList) {
        const QVariantMap map = value.toMap();
        const auto clip = TimelineCodec::clipFromMap(map, &error);
        if (!clip) return failure(tr("Clip: %1").arg(error));
        if (clip->anchor == TimelineClip::Anchor::Musical && !map.contains(QStringLiteral("musicalTick")))
            unanchored.push_back(clips.size());
        clips.push_back(*clip);
    }
    // Musical anchors are resolved with this model's timebase, so a saved
    // position is only a hint; a missing musicalTick derives from it.
    const TimelineRate previous = m_timebase;
    m_timebase = timebase;
    QVector<TimelineMarker> markers;
    for (const auto &value : content.value(QStringLiteral("markers")).toList()) {
        const QVariantMap map = value.toMap();
        auto marker = TimelineCodec::markerFromMap(map, &error);
        if (!marker || marker->id.isNull()) {
            m_timebase = previous;
            return failure(tr("Marker: %1").arg(marker ? tr("missing id") : error));
        }
        if (marker->anchor == TimelineMarker::Anchor::Musical && !map.contains(QStringLiteral("musicalTick"))) {
            const auto tick = musicalTickFor(marker->position);
            if (!tick) {
                m_timebase = previous;
                return failure(tr("Marker: cannot derive a musical position"));
            }
            marker->musicalTick = *tick;
        }
        markers.push_back(*marker);
    }
    for (const qsizetype index : unanchored) clips[index] = anchoredClip(clips[index]);
    m_timebase = previous;
    QUuid documentId;
    if (content.contains(QStringLiteral("documentId"))) {
        documentId = TimelineCodec::uuid(content.value(QStringLiteral("documentId")));
        if (documentId.isNull()) return failure(tr("documentId must be a UUID string"));
    }
    OperationError failed;
    if (!setContent(timebase, tracks, clips, markers, &failed, documentId)) {
        fail(failed.message);
        return failure(failed.message);
    }
    const QVariant range = content.value(QStringLiteral("editRange"));
    if (range.isValid() && !range.isNull()) {
        const QVariantMap map = range.toMap();
        const auto start = TimelineCodec::ticks(map.value(QStringLiteral("start")));
        const auto end = TimelineCodec::ticks(map.value(QStringLiteral("end")));
        if (start && end) setEditRangeTicks(*start, *end);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("revision"), revisionText()},
            {QStringLiteral("trackCount"), trackCount()}, {QStringLiteral("clipCount"), clipCount()},
            {QStringLiteral("markerCount"), markerCount()}};
}

QVariantMap TimelineModel::contentMap() const
{
    QVariantList tracks, clips, markers;
    QHash<QUuid, int> order;
    for (int i = 0; i < m_trackOrder.size(); ++i) {
        order.insert(m_trackOrder.at(i), i);
        tracks.push_back(TimelineCodec::toMap(m_tracks.value(m_trackOrder.at(i))));
    }
    QVector<const TimelineClip *> sortedClips;
    sortedClips.reserve(m_clips.size());
    for (auto it = m_clips.cbegin(); it != m_clips.cend(); ++it) sortedClips.push_back(&it.value());
    std::sort(sortedClips.begin(), sortedClips.end(), [&](const auto *a, const auto *b) {
        const int ta = order.value(a->trackId), tb = order.value(b->trackId);
        if (ta != tb) return ta < tb;
        if (a->start != b->start) return a->start < b->start;
        return a->id < b->id;
    });
    for (const auto *clip : sortedClips) {
        QVariantMap map = TimelineCodec::toMap(*clip);
        map.remove(QStringLiteral("end"));
        clips.push_back(map);
    }
    QVector<const TimelineMarker *> sortedMarkers;
    for (auto it = m_markers.cbegin(); it != m_markers.cend(); ++it) sortedMarkers.push_back(&it.value());
    std::sort(sortedMarkers.begin(), sortedMarkers.end(), [](const auto *a, const auto *b) {
        return a->position != b->position ? a->position < b->position : a->id < b->id;
    });
    for (const auto *marker : sortedMarkers) markers.push_back(TimelineCodec::toMap(*marker));
    QVariantMap content{{QStringLiteral("schema"), schemaId()},
                        {QStringLiteral("documentId"), documentId()},
                        {QStringLiteral("timebase"), QVariantMap{
                            {QStringLiteral("numerator"), timebaseNumeratorText()},
                            {QStringLiteral("denominator"), timebaseDenominatorText()}}},
                        {QStringLiteral("tracks"), tracks},
                        {QStringLiteral("clips"), clips},
                        {QStringLiteral("markers"), markers}};
    content.insert(QStringLiteral("editRange"),
                   m_hasRange ? QVariant(QVariantMap{{QStringLiteral("start"), editRangeStartText()},
                                                     {QStringLiteral("end"), editRangeEndText()}})
                              : QVariant());
    return content;
}

QString TimelineModel::toJson() const
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(contentMap()))
                                 .toJson(QJsonDocument::Compact));
}

QVariantMap TimelineModel::loadJson(const QString &json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(tr("The timeline file is not a JSON object"));
        return failure(tr("The timeline file is not a JSON object"));
    }
    const QVariantMap content = document.object().toVariantMap();
    if (content.value(QStringLiteral("schema")).toString() != schemaId()) {
        fail(tr("Unsupported timeline schema"));
        return failure(tr("Unsupported timeline schema"));
    }
    return loadContent(content);
}

void TimelineModel::clear()
{
    setContent(m_timebase, {}, {}, {});
}

bool TimelineModel::setTimebase(const QString &numerator, const QString &denominator)
{
    const auto n = TimelineTime::parseDecimal(numerator);
    const auto d = TimelineTime::parseDecimal(denominator);
    const auto rate = n && d ? TimelineRate::normalized(*n, *d) : std::nullopt;
    if (!rate) return fail(tr("The timebase must be a positive rational of decimal strings"));
    if (!m_clips.isEmpty() || !m_markers.isEmpty())
        return fail(tr("The timebase can change only while the timeline has no clips or markers"));
    if (*rate == m_timebase) return true;
    QVector<TimelineTrack> tracks;
    for (const auto &id : m_trackOrder) tracks.push_back(m_tracks.value(id));
    return setContent(*rate, tracks, {}, {});
}

} // namespace QindaTK
