// SPDX-License-Identifier: LGPL-3.0-or-later
// Channel layouts, speaker names and the spatial panner (D-111). The gain
// functions are realtime safe: AudioRenderGraph::render calls them per sample
// when pan or depth is automated.
#include "audio_mixer_types.h"

#include <algorithm>
#include <cmath>

namespace QindaTK::AudioMixer {
namespace {

// -3 dB, the ITU-R BS.775 downmix coefficient.
constexpr double kMinus3dB = 0.70710678118654752440;

} // namespace

QString layoutName(AudioLayout layout)
{
    switch (layout) {
    case AudioLayout::Mono: return QStringLiteral("Mono");
    case AudioLayout::Quad: return QStringLiteral("Quad");
    case AudioLayout::Surround51: return QStringLiteral("5.1");
    case AudioLayout::Stereo: break;
    }
    return QStringLiteral("Stereo");
}

std::optional<AudioLayout> layoutFromName(const QString &name)
{
    for (AudioLayout layout : {AudioLayout::Mono, AudioLayout::Stereo, AudioLayout::Quad, AudioLayout::Surround51})
        if (layoutName(layout) == name) return layout;
    return std::nullopt;
}

QStringList layoutNames()
{
    return {QStringLiteral("Mono"), QStringLiteral("Stereo"), QStringLiteral("Quad"), QStringLiteral("5.1")};
}

QStringList trackLayoutNames() { return {QStringLiteral("Mono"), QStringLiteral("Stereo")}; }

std::optional<AudioLayout> layoutForLanes(int lanes)
{
    switch (lanes) {
    case 1: return AudioLayout::Mono;
    case 2: return AudioLayout::Stereo;
    case 4: return AudioLayout::Quad;
    case 6: return AudioLayout::Surround51;
    default: return std::nullopt;
    }
}

bool isSurround(AudioLayout layout)
{
    return layout == AudioLayout::Quad || layout == AudioLayout::Surround51;
}

QStringList speakerNames(AudioLayout layout)
{
    switch (layout) {
    case AudioLayout::Mono: return {QStringLiteral("M")};
    case AudioLayout::Quad: return {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("Ls"), QStringLiteral("Rs")};
    case AudioLayout::Surround51:
        return {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("LFE"),
                QStringLiteral("Ls"), QStringLiteral("Rs")};
    case AudioLayout::Stereo: break;
    }
    return {QStringLiteral("L"), QStringLiteral("R")};
}

void spatialGains(AudioLayout layout, AudioPanLaw law, double x, double y, double *gains) noexcept
{
    std::fill_n(gains, kMaxLanes, 0.0);
    if (!isSurround(layout)) return;
    double left = 0.0, right = 0.0, rear = 0.0, front = 0.0;
    panGains(law, x, left, right);
    panGains(law, y, rear, front); // -1 is the rear pair, +1 the front
    if (layout == AudioLayout::Quad) {
        gains[0] = left * front;
        gains[1] = right * front;
        gains[2] = left * rear;
        gains[3] = right * rear;
        return;
    }
    // 5.1: the front pans L-C (x <= 0) or C-R (x > 0) with the same law, so a
    // centred source plays on the centre speaker alone; the rear pair as Quad.
    const double p = std::isfinite(x) ? std::clamp(x, -1.0, 1.0) : 0.0;
    double a = 0.0, b = 0.0;
    if (p <= 0.0) {
        panGains(law, 2.0 * p + 1.0, a, b);
        gains[0] = a * front;
        gains[2] = b * front;
    } else {
        panGains(law, 2.0 * p - 1.0, a, b);
        gains[2] = a * front;
        gains[1] = b * front;
    }
    gains[4] = left * rear;
    gains[5] = right * rear;
}

void surroundMatrix(AudioLayout from, AudioLayout to, double *matrix) noexcept
{
    std::fill_n(matrix, kMaxLanes * kMaxLanes, 0.0);
    const auto set = [matrix](int out, int in, double gain) noexcept { matrix[out * kMaxLanes + in] += gain; };
    if (from == to) {
        for (int lane = 0; lane < int(from); ++lane) set(lane, lane, 1.0);
        return;
    }
    // Source lanes by speaker: L R (C) Ls Rs; -1 where the layout has none.
    const bool five = from == AudioLayout::Surround51;
    const int l = 0, r = 1, c = five ? 2 : -1, ls = five ? 4 : 2, rs = five ? 5 : 3;
    switch (to) {
    case AudioLayout::Quad:
    case AudioLayout::Surround51: {
        const bool toFive = to == AudioLayout::Surround51;
        set(0, l, 1.0);
        set(1, r, 1.0);
        set(toFive ? 4 : 2, ls, 1.0);
        set(toFive ? 5 : 3, rs, 1.0);
        if (c >= 0) { // only 5.1 to Quad reaches here with a centre
            set(0, c, kMinus3dB);
            set(1, c, kMinus3dB);
        }
        return;
    }
    case AudioLayout::Stereo:
    case AudioLayout::Mono: {
        const double share = to == AudioLayout::Mono ? 0.5 : 1.0;
        const int right = to == AudioLayout::Mono ? 0 : 1;
        set(0, l, share);
        set(right, r, share);
        set(0, ls, kMinus3dB * share);
        set(right, rs, kMinus3dB * share);
        if (c >= 0) {
            set(0, c, kMinus3dB * share);
            set(right, c, kMinus3dB * share);
        }
        return;
    }
    }
}

} // namespace QindaTK::AudioMixer
