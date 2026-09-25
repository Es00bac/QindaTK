// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "audio_media.h"

#include <memory>
#include <vector>

namespace QindaTK {

// AGENT-CONTRACT (D-135, docs/audio-editing.md): a min/max pyramid over one
// immutable AudioBuffer. Level 0 holds the extrema of kBaseBlock-sample
// blocks, each next level of kFactor blocks of the level below, until a level
// has at most kFactor blocks. extrema() combines whole blocks from the
// coarsest level that fits and reads raw samples only at the partial edges,
// so every answer is the exact min/max of the samples: no peak is ever lost
// to reduction. Memory is about 2/3 byte per sample per channel.
class AudioPeakPyramid {
public:
    static constexpr int kBaseBlock = 16;
    static constexpr int kFactor = 4;
    static constexpr int kMaxColumns = 8192;

    struct Extrema {
        float minimum = 0.0f;
        float maximum = 0.0f;
    };
    struct Column {
        qint64 start = 0; // first sample, inclusive
        qint64 end = 0;   // exclusive
        float minimum = 0.0f;
        float maximum = 0.0f;
    };

    // Runs on any thread; checks `cancel` between blocks and returns null
    // when cancelled or when the buffer is empty.
    static std::shared_ptr<const AudioPeakPyramid> build(AudioBufferPtr buffer, const CancellationToken &cancel = {});

    const AudioBufferPtr &buffer() const { return m_buffer; }
    int levels() const { return int(m_levels.size()); }
    qint64 blockSize(int level) const;
    qint64 memoryBytes() const;

    // Exact min/max over [start, end) of `channel`, or of every channel when
    // channel < 0. The range is clamped to the buffer; an empty range is {0, 0}.
    Extrema extrema(int channel, qint64 start, qint64 end) const;
    // min(count, end - start, kMaxColumns) columns over [start, end): column i
    // covers [start + floor(i·L/n), start + floor((i+1)·L/n)), L = end - start.
    // When L <= count every column is one sample (min == max == the sample).
    std::vector<Column> columns(int channel, qint64 start, qint64 end, int count) const;

private:
    void combine(int channel, int level, qint64 start, qint64 end, Extrema &out, bool &any) const;
    AudioBufferPtr m_buffer;
    // m_levels[level][channel][block]
    std::vector<std::vector<std::vector<Extrema>>> m_levels;
};
using AudioPeakPyramidPtr = std::shared_ptr<const AudioPeakPyramid>;

} // namespace QindaTK
