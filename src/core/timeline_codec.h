// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_types.h"

#include <QVariantMap>

namespace QindaTK {

// QML/JSON representation of timeline records. Every tick value is a
// decimal string (a QML Number is rejected, as in ContractCodec), IDs are
// UUID strings, and booleans/heights are ordinary values. `base` lets a
// partial map update an existing record field by field.
namespace TimelineCodec {
std::optional<qint64> ticks(const QVariant &value);
QUuid uuid(const QVariant &value);
QVariantMap toMap(const TimelineTrack &track);
QVariantMap toMap(const TimelineClip &clip);
QVariantMap toMap(const TimelineMarker &marker);
std::optional<TimelineTrack> trackFromMap(const QVariantMap &map, QString *error,
                                          const TimelineTrack *base = nullptr);
std::optional<TimelineClip> clipFromMap(const QVariantMap &map, QString *error,
                                        const TimelineClip *base = nullptr);
std::optional<TimelineMarker> markerFromMap(const QVariantMap &map, QString *error,
                                            const TimelineMarker *base = nullptr);
QVariantMap changeToMap(const TimelineChange &change);
std::optional<TimelineChange> changeFromMap(const QVariantMap &map, QString *error);
} // namespace TimelineCodec

} // namespace QindaTK
