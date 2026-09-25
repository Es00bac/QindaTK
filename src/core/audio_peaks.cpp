// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_peaks.h"

#include <algorithm>

namespace QindaTK {
namespace {

void merge(AudioPeakPyramid::Extrema &out, bool &any, float minimum, float maximum)
{
    if (!any) {
        out = {minimum, maximum};
        any = true;
        return;
    }
    out.minimum = std::min(out.minimum, minimum);
    out.maximum = std::max(out.maximum, maximum);
}

} // namespace

AudioPeakPyramidPtr AudioPeakPyramid::build(AudioBufferPtr buffer, const CancellationToken &cancel)
{
    if (!buffer || buffer->frames <= 0 || buffer->channels <= 0) return nullptr;
    auto pyramid = std::make_shared<AudioPeakPyramid>();
    pyramid->m_buffer = buffer;
    const qint64 frames = buffer->frames;
    const int channels = buffer->channels;
    // Level 0 from the samples.
    const qint64 baseBlocks = (frames + kBaseBlock - 1) / kBaseBlock;
    auto &base = pyramid->m_levels.emplace_back(size_t(channels));
    for (int c = 0; c < channels; ++c) {
        const float *samples = buffer->channel(c);
        auto &blocks = base[size_t(c)];
        blocks.resize(size_t(baseBlocks));
        for (qint64 b = 0; b < baseBlocks; ++b) {
            if ((b & 4095) == 0 && cancel.isCancelled()) return nullptr;
            const qint64 first = b * kBaseBlock;
            const qint64 last = std::min(frames, first + kBaseBlock);
            float lo = samples[first], hi = samples[first];
            for (qint64 i = first + 1; i < last; ++i) {
                lo = std::min(lo, samples[i]);
                hi = std::max(hi, samples[i]);
            }
            blocks[size_t(b)] = {lo, hi};
        }
    }
    // Coarser levels, each block the extrema of kFactor blocks below.
    while (pyramid->m_levels.back().front().size() > size_t(kFactor)) {
        if (cancel.isCancelled()) return nullptr;
        const auto &below = pyramid->m_levels.back();
        std::vector<std::vector<Extrema>> level(static_cast<size_t>(channels));
        for (int c = 0; c < channels; ++c) {
            const auto &source = below[size_t(c)];
            auto &blocks = level[size_t(c)];
            blocks.resize((source.size() + kFactor - 1) / kFactor);
            for (size_t b = 0; b < blocks.size(); ++b) {
                Extrema e = source[b * kFactor];
                for (size_t i = b * kFactor + 1; i < std::min(source.size(), (b + 1) * kFactor); ++i) {
                    e.minimum = std::min(e.minimum, source[i].minimum);
                    e.maximum = std::max(e.maximum, source[i].maximum);
                }
                blocks[b] = e;
            }
        }
        pyramid->m_levels.push_back(std::move(level));
    }
    if (cancel.isCancelled()) return nullptr;
    return pyramid;
}

qint64 AudioPeakPyramid::blockSize(int level) const
{
    qint64 size = kBaseBlock;
    for (int l = 0; l < level; ++l) size *= kFactor;
    return size;
}

qint64 AudioPeakPyramid::memoryBytes() const
{
    qint64 bytes = 0;
    for (const auto &level : m_levels)
        for (const auto &channel : level) bytes += qint64(channel.size() * sizeof(Extrema));
    return bytes;
}

// Whole blocks of `level` inside [start, end), then the two partial edges one
// level down; level -1 reads the samples. Each level visits fewer than
// 2·kFactor blocks below the coarsest one that fits.
void AudioPeakPyramid::combine(int channel, int level, qint64 start, qint64 end, Extrema &out, bool &any) const
{
    if (start >= end) return;
    if (level < 0) {
        const float *samples = m_buffer->channel(channel);
        for (qint64 i = start; i < end; ++i) merge(out, any, samples[i], samples[i]);
        return;
    }
    const qint64 size = blockSize(level);
    const qint64 first = (start + size - 1) / size;
    const qint64 last = end / size; // exclusive; a partial final block is never whole here
    if (first >= last) {
        combine(channel, level - 1, start, end, out, any);
        return;
    }
    const auto &blocks = m_levels[size_t(level)][size_t(channel)];
    for (qint64 b = first; b < last; ++b) merge(out, any, blocks[size_t(b)].minimum, blocks[size_t(b)].maximum);
    combine(channel, level - 1, start, first * size, out, any);
    combine(channel, level - 1, last * size, end, out, any);
}

AudioPeakPyramid::Extrema AudioPeakPyramid::extrema(int channel, qint64 start, qint64 end) const
{
    Extrema out;
    if (!m_buffer) return out;
    start = std::clamp<qint64>(start, 0, m_buffer->frames);
    end = std::clamp<qint64>(end, start, m_buffer->frames);
    if (start >= end || channel >= m_buffer->channels) return out;
    // The coarsest level whose block fits in the range.
    int level = -1;
    for (int l = 0; l < levels() && blockSize(l) <= end - start; ++l) level = l;
    bool any = false;
    for (int c = channel < 0 ? 0 : channel; c < (channel < 0 ? m_buffer->channels : channel + 1); ++c)
        combine(c, level, start, end, out, any);
    return out;
}

std::vector<AudioPeakPyramid::Column> AudioPeakPyramid::columns(int channel, qint64 start, qint64 end,
                                                               int count) const
{
    std::vector<Column> out;
    if (!m_buffer) return out;
    start = std::clamp<qint64>(start, 0, m_buffer->frames);
    end = std::clamp<qint64>(end, start, m_buffer->frames);
    const qint64 length = end - start;
    const qint64 n = std::min<qint64>({qint64(std::max(0, count)), length, kMaxColumns});
    out.reserve(size_t(n));
    for (qint64 i = 0; i < n; ++i) {
        // i ≤ 8192 and length < 2^40 in practice (1 GiB media cap), so the
        // products stay far inside qint64.
        Column column;
        column.start = start + i * length / n;
        column.end = start + (i + 1) * length / n;
        const Extrema e = extrema(channel, column.start, column.end);
        column.minimum = e.minimum;
        column.maximum = e.maximum;
        out.push_back(column);
    }
    return out;
}

} // namespace QindaTK
