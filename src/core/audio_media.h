// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QFile>
#include <QString>
#include <memory>
#include <vector>

namespace QindaTK {

// Decoded audio in planar float32: channel c occupies
// samples[c * frames, (c + 1) * frames). Immutable once shared.
struct AudioBuffer {
    int sampleRate = 0;
    int channels = 0;
    qint64 frames = 0;
    std::vector<float> samples;
    const float *channel(int index) const { return samples.data() + qsizetype(index) * frames; }
    float *channel(int index) { return samples.data() + qsizetype(index) * frames; }
};
using AudioBufferPtr = std::shared_ptr<const AudioBuffer>;

// AGENT-NOTE: the reference engine's media codec is RIFF/WAVE only (PCM 8/16/
// 24/32-bit integer, 32/64-bit IEEE float, WAVE_FORMAT_EXTENSIBLE). Other
// containers belong to format adapters (WP-019), not to the engine core.
namespace AudioFile {
inline constexpr qint64 kMaximumBytes = qint64(1) << 30;
AudioBufferPtr readWav(const QString &path, QString *error = nullptr);
// Writes 32-bit float WAV so rendered and recorded samples stay bit-exact.
bool writeWav(const QString &path, const AudioBuffer &buffer, QString *error = nullptr);
// Band-limited (Blackman-windowed sinc, 32 zero crossings) conversion that
// keeps duration: frames * target / source, rounded to nearest.
AudioBufferPtr resample(const AudioBuffer &source, int targetRate);
} // namespace AudioFile

// Streaming float32 WAV writer (recorder takes, offline render). Sizes are
// patched by finish(); an unfinished file is not a valid WAV.
class AudioWavWriter {
public:
    bool open(const QString &path, int sampleRate, int channels, QString *error = nullptr);
    bool write(const float *interleaved, qint64 frames);
    bool finish(QString *error = nullptr);
    qint64 frames() const { return m_frames; }
    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }
private:
    QFile m_file;
    int m_channels = 0;
    qint64 m_frames = 0;
    bool m_failed = false;
};

} // namespace QindaTK
