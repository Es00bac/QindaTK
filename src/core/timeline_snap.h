// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_types.h"

#include <QSet>

namespace QindaTK {

class TimelineModel;

struct TimelineSnapContext {
    qint64 tolerance = 0;                     // ticks; 0 disables snapping
    const QSet<QUuid> *excludedClips = nullptr;
    QUuid excludedMarker;
    std::optional<qint64> playhead;
    TimelineSnapGrid grid = TimelineSnapGrid::None;
    bool clipEdges = true;
    bool markers = true;
    bool editRange = true;
    bool origin = true;
};

// AGENT-NOTE: timeline snapping is one-dimensional over exact ticks. WP-041's
// spatial SnapModel (CAN-04) was not available when this was written; when it
// lands, keep this as the time-axis provider behind it rather than routing
// exact ticks through floating document coordinates.
// Targets: committed clip starts/ends (moving clips excluded), marker
// positions and region ends, the playhead, the edit range, time zero and grid
// lines. Nearest distance wins; ties prefer clip edges, then markers,
// playhead, range, origin, grid, then the earlier tick and earlier candidate.
class TimelineSnapper {
public:
    static TimelineSnapResult snap(const TimelineModel &model, const QVector<qint64> &candidates,
                                   const TimelineSnapContext &context);
};

} // namespace QindaTK
