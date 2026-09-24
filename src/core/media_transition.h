// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

namespace QindaTK {

// AGENT-CONTRACT (docs/sequence-edits.md "Evaluation", D-120): the one
// transition evaluator shared by playback, monitors and export, so a preview
// and an exported file show the same frames. A program frame [start, end)
// inside a window [windowStart, windowStart + duration) takes the incoming
// weight at the frame's midpoint, rounded once to 1/65536:
//   w = round(65536 * (2 * (start - windowStart) + (end - start)) / (2 * duration)).
// Pictures blend per premultiplied channel as (a * (65536 - w) + b * w + 32768) >> 16.
namespace MediaTransition {
inline constexpr int kWeightOne = 65536;

// Incoming weight 0..65536 for one frame (see above); 0 outside the window.
int frameWeight(qint64 frameStart, qint64 frameEnd, qint64 windowStart, qint64 windowDuration);

// outgoing/incoming: equal-sized ARGB32_Premultiplied layers.
//  dissolve: channel mix by w.
//  wipe:     parameters.direction left (default), right, up or down: the incoming
//            picture covers round(extent * w / 65536) columns/rows from that side.
//  dip:      parameters.color (default black): outgoing mixes to the colour by 2w,
//            then the colour to the incoming picture by 2w - 65536.
QImage blend(const QImage &outgoing, const QImage &incoming, const QString &type, int weight,
             const QVariantMap &parameters);

// Audio gain of the incoming (fadeIn) or outgoing side at program sample
// `sample` in the window [first, first + count) of samples. Linear by default:
// g = (sample - first + 0.5) / count; parameters.curve "equalPower" uses
// sin(g * pi / 2) (incoming) and cos(g * pi / 2) (outgoing). 1 outside.
float audioGain(qint64 sample, qint64 first, qint64 count, bool incoming, const QVariantMap &parameters);
} // namespace MediaTransition

} // namespace QindaTK
