// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_media.h"

#include <QCoreApplication>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <numbers>

namespace QindaTK {
namespace {

// AGENT-NOTE: float WAV payloads are written with a raw copy, which is only
// the RIFF byte order on little-endian hosts (every supported target).
static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "AudioWavWriter writes host floats as little-endian");

constexpr quint16 kFormatPcm = 1;
constexpr quint16 kFormatFloat = 3;
constexpr quint16 kFormatExtensible = 0xFFFE;

QString text(const char *message)
{
    return QCoreApplication::translate("QindaTK::AudioFile", message);
}

template <typename T>
T fail(QString *error, const QString &message, T value = {})
{
    if (error) *error = message;
    return value;
}

quint16 u16(const char *p) { return qFromLittleEndian<quint16>(p); }
quint32 u32(const char *p) { return qFromLittleEndian<quint32>(p); }

float decode(const char *p, quint16 format, int bits)
{
    if (format == kFormatFloat)
        return bits == 64 ? float(qFromLittleEndian<double>(p)) : qFromLittleEndian<float>(p);
    switch (bits) {
    case 8: return float(int(uchar(p[0])) - 128) / 128.0f;
    case 16: return float(qFromLittleEndian<qint16>(p)) / 32768.0f;
    case 24: {
        qint32 value = qint32(uchar(p[0])) | qint32(uchar(p[1])) << 8 | qint32(uchar(p[2])) << 16;
        if (value & 0x800000) value -= 0x1000000;
        return float(value) / 8388608.0f;
    }
    default: return float(double(qFromLittleEndian<qint32>(p)) / 2147483648.0);
    }
}

QByteArray floatHeader(int sampleRate, int channels, qint64 frames)
{
    const quint32 dataBytes = quint32(frames * channels * 4);
    QByteArray header(46, '\0');
    char *h = header.data();
    std::memcpy(h, "RIFF", 4);
    qToLittleEndian<quint32>(38 + dataBytes, h + 4);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(18, h + 16);
    qToLittleEndian<quint16>(kFormatFloat, h + 20);
    qToLittleEndian<quint16>(quint16(channels), h + 22);
    qToLittleEndian<quint32>(quint32(sampleRate), h + 24);
    qToLittleEndian<quint32>(quint32(sampleRate * channels * 4), h + 28);
    qToLittleEndian<quint16>(quint16(channels * 4), h + 32);
    qToLittleEndian<quint16>(32, h + 34);
    qToLittleEndian<quint16>(0, h + 36); // cbSize
    std::memcpy(h + 38, "data", 4);
    qToLittleEndian<quint32>(dataBytes, h + 42);
    return header;
}

} // namespace

AudioBufferPtr AudioFile::readWav(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail<AudioBufferPtr>(error, text("The audio file cannot be opened: %1").arg(file.errorString()));
    if (file.size() > kMaximumBytes)
        return fail<AudioBufferPtr>(error, text("The audio file exceeds the 1 GiB reference-codec limit"));
    const QByteArray bytes = file.readAll();
    const char *data = bytes.constData();
    if (bytes.size() < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return fail<AudioBufferPtr>(error, text("Only RIFF/WAVE audio files are supported"));

    quint16 format = 0, channels = 0, blockAlign = 0, bits = 0;
    quint32 rate = 0;
    const char *payload = nullptr;
    qint64 payloadBytes = 0;
    for (qint64 at = 12; at + 8 <= bytes.size();) {
        const char *chunk = data + at;
        const qint64 size = u32(chunk + 4);
        const qint64 available = std::min<qint64>(size, bytes.size() - at - 8);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && available >= 16) {
            format = u16(chunk + 8);
            channels = u16(chunk + 10);
            rate = u32(chunk + 12);
            blockAlign = u16(chunk + 20);
            bits = u16(chunk + 22);
            if (format == kFormatExtensible && available >= 40) format = u16(chunk + 32);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            payload = chunk + 8;
            payloadBytes = available;
        }
        at += 8 + size + (size & 1);
    }
    const bool pcm = format == kFormatPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool ieee = format == kFormatFloat && (bits == 32 || bits == 64);
    if (!payload || (!pcm && !ieee))
        return fail<AudioBufferPtr>(error, text("The WAV file has no supported PCM or float sample data"));
    if (channels < 1 || channels > 32 || rate < 1 || rate > 768000 || blockAlign != channels * (bits / 8))
        return fail<AudioBufferPtr>(error, text("The WAV format header is inconsistent"));

    auto buffer = std::make_shared<AudioBuffer>();
    buffer->sampleRate = int(rate);
    buffer->channels = channels;
    buffer->frames = payloadBytes / blockAlign;
    buffer->samples.resize(size_t(buffer->frames) * channels);
    const int width = bits / 8;
    for (qint64 frame = 0; frame < buffer->frames; ++frame)
        for (int c = 0; c < channels; ++c)
            buffer->channel(c)[frame] = decode(payload + frame * blockAlign + c * width, format, bits);
    return buffer;
}

bool AudioFile::writeWav(const QString &path, const AudioBuffer &buffer, QString *error)
{
    AudioWavWriter writer;
    if (!writer.open(path, buffer.sampleRate, buffer.channels, error)) return false;
    std::vector<float> block(size_t(4096) * buffer.channels);
    for (qint64 at = 0; at < buffer.frames; at += 4096) {
        const qint64 count = std::min<qint64>(4096, buffer.frames - at);
        for (qint64 i = 0; i < count; ++i)
            for (int c = 0; c < buffer.channels; ++c)
                block[size_t(i * buffer.channels + c)] = buffer.channel(c)[at + i];
        writer.write(block.data(), count);
    }
    return writer.finish(error);
}

AudioBufferPtr AudioFile::resample(const AudioBuffer &source, int targetRate)
{
    auto out = std::make_shared<AudioBuffer>();
    if (source.sampleRate == targetRate || targetRate < 1 || source.sampleRate < 1) {
        *out = source;
        return out;
    }
    const double ratio = double(targetRate) / double(source.sampleRate);
    out->sampleRate = targetRate;
    out->channels = source.channels;
    out->frames = qint64(std::llround(double(source.frames) * ratio));
    out->samples.assign(size_t(out->frames) * source.channels, 0.0f);
    constexpr double kZeroCrossings = 32.0;
    const double cutoff = std::min(1.0, ratio) * 0.95;
    const int half = int(std::ceil(kZeroCrossings / cutoff));
    const auto kernel = [cutoff, half](double x) {
        const double u = x / half;
        if (u <= -1.0 || u >= 1.0) return 0.0;
        const double window = 0.42 + 0.5 * std::cos(std::numbers::pi * u) + 0.08 * std::cos(2 * std::numbers::pi * u);
        const double y = cutoff * x;
        const double sinc = y == 0.0 ? 1.0 : std::sin(std::numbers::pi * y) / (std::numbers::pi * y);
        return cutoff * sinc * window;
    };
    for (qint64 n = 0; n < out->frames; ++n) {
        const double t = double(n) / ratio;
        const qint64 centre = qint64(std::floor(t));
        const qint64 first = std::max<qint64>(0, centre - half + 1);
        const qint64 last = std::min<qint64>(source.frames - 1, centre + half);
        for (int c = 0; c < source.channels; ++c) {
            const float *in = source.channel(c);
            double sum = 0.0;
            for (qint64 k = first; k <= last; ++k) sum += in[k] * kernel(t - double(k));
            out->channel(c)[n] = float(sum);
        }
    }
    return out;
}

bool AudioWavWriter::open(const QString &path, int sampleRate, int channels, QString *error)
{
    if (m_file.isOpen()) m_file.close();
    m_channels = channels;
    m_frames = 0;
    m_failed = false;
    if (sampleRate < 1 || channels < 1)
        return fail<bool>(error, text("A WAV file needs a sample rate and at least one channel"));
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail<bool>(error, text("The audio file cannot be written: %1").arg(m_file.errorString()));
    if (m_file.write(floatHeader(sampleRate, channels, 0)) != 46) {
        m_file.close();
        return fail<bool>(error, text("The WAV header cannot be written"));
    }
    return true;
}

bool AudioWavWriter::write(const float *interleaved, qint64 frames)
{
    if (!m_file.isOpen() || m_failed) return false;
    const qint64 bytes = frames * m_channels * qint64(sizeof(float));
    if (m_file.write(reinterpret_cast<const char *>(interleaved), bytes) != bytes) {
        m_failed = true;
        return false;
    }
    m_frames += frames;
    return true;
}

bool AudioWavWriter::finish(QString *error)
{
    if (!m_file.isOpen()) return fail<bool>(error, text("The WAV file is not open"));
    const qint64 dataBytes = m_frames * m_channels * 4;
    bool ok = !m_failed && dataBytes <= 0xFFFFFFFFll - 38;
    if (ok) {
        const QByteArray header = floatHeader(0, m_channels, m_frames);
        ok = m_file.seek(4) && m_file.write(header.constData() + 4, 4) == 4
             && m_file.seek(42) && m_file.write(header.constData() + 42, 4) == 4;
    }
    const QString message = m_file.errorString();
    m_file.close();
    if (!ok) return fail<bool>(error, text("The WAV file could not be completed: %1").arg(message));
    return true;
}

} // namespace QindaTK
