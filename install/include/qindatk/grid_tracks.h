// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace QindaTK {

// One bound of a grid track: `200` (px), `25%`, `auto`, `1fr`, or
// `min-content`/`max-content` (treated as auto).
struct GridLength final {
    enum Kind { Fixed, Percent, Auto, Fr, Content };
    Kind kind = Auto;
    qreal value = 0;
    [[nodiscard]] bool contentSized() const { return kind == Auto || kind == Content; }
    [[nodiscard]] bool operator==(const GridLength &) const = default;
};

// A track as `minmax(min, max)`. Plain `auto` is minmax(auto, auto);
// `200` is minmax(200, 200); `1fr` is minmax(0, 1fr).
struct GridTrack final {
    GridLength min;
    GridLength max;
    [[nodiscard]] bool flexible() const { return max.kind == GridLength::Fr; }
    [[nodiscard]] bool operator==(const GridTrack &) const = default;
};

// AGENT-CONTRACT: the track-list grammar accepted by Grid.columns/rows:
//   <track>+ where <track> = <length> | minmax(<length>, <length>)
//                          | repeat(<count>, <track>+)
// `error` receives a message for an unparsable spec; the tracks parsed so
// far are returned. tests/cpp/tst_grid_tracks.cpp is the reference.
[[nodiscard]] QVector<GridTrack> parseGridTracks(const QString &spec, QString *error = nullptr);

// An item's size demand on one axis: the tracks it spans and the size its
// content asks for (implicit size clamped by its min/max).
struct GridContribution final {
    int start = 0;
    int span = 1;
    qreal size = 0;
};

struct GridTrackSizes final {
    QVector<qreal> sizes;     // final track sizes for the given available size
    QVector<qreal> natural;   // content-driven sizes, for the container's implicit size
};

// Sizes tracks for `available` pixels (<= 0 means indefinite: no free
// space, fr tracks collapse to their content). Follows CSS Grid Layout
// Level 1 §11 with these deliberate simplifications: spanning items do not
// grow fr tracks, percent tracks in an indefinite container act as auto,
// and `Nfr` has a 0 minimum (see docs/decisions.md, D-004).
[[nodiscard]] GridTrackSizes sizeGridTracks(const QVector<GridTrack> &tracks,
                                            const QVector<GridContribution> &items,
                                            qreal available, qreal gap);

} // namespace QindaTK
