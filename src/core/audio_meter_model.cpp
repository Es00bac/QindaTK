// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_meter_model.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {
constexpr int maximumChannels = 64;
constexpr qsizetype maximumFixtureSamples = 1000000;

QString number(double value)
{
    return QString::number(value, 'g', 6);
}
}

AudioBlockLevels measureInterleaved(std::span<const float> samples, int channels)
{
    AudioBlockLevels result;
    if (channels < 1 || channels > maximumChannels || samples.size() % size_t(channels))
        return result;
    result.peaks.fill(0, channels);
    result.rms.fill(0, channels);
    QVector<qsizetype> counts(channels, 0);
    const qsizetype frames = qsizetype(samples.size()) / channels;
    for (qsizetype frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const float sample = samples[size_t(frame) * channels + channel];
            if (!std::isfinite(sample)) { ++result.invalidSamples; continue; }
            const double magnitude = std::abs(double(sample));
            result.peaks[channel] = std::max(result.peaks.at(channel), magnitude);
            result.rms[channel] += magnitude * magnitude;
            ++counts[channel];
        }
    }
    for (int channel = 0; channel < channels; ++channel)
        result.rms[channel] = counts.at(channel) > 0
            ? std::sqrt(result.rms.at(channel) / counts.at(channel)) : 0;
    return result;
}

double AudioMeterModel::toDbfs(double linear)
{
    return linear > 0 && std::isfinite(linear)
        ? std::max(kFloorDbfs, 20 * std::log10(linear)) : kFloorDbfs;
}

QStringList AudioMeterModel::channelNames(const QString &layout, int channels)
{
    if (layout == QLatin1String("Mono") && channels == 1) return {QStringLiteral("M")};
    if (layout == QLatin1String("Stereo") && channels == 2) return {QStringLiteral("L"), QStringLiteral("R")};
    if (layout == QLatin1String("Quad") && channels == 4)
        return {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("Ls"), QStringLiteral("Rs")};
    if (layout == QLatin1String("5.1") && channels == 6)
        return {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("LFE"), QStringLiteral("Ls"),
                QStringLiteral("Rs")};
    QStringList names;
    for (int index = 0; index < channels; ++index) names.append(QString::number(index + 1));
    return names;
}

QVariantList AudioMeterModel::levels() const
{
    QVariantList result;
    result.reserve(m_channels.size());
    for (int index = 0; index < m_channels.size(); ++index) {
        const Channel &channel = m_channels.at(index);
        result.append(QVariantMap{{QStringLiteral("channel"), index},
                                  {QStringLiteral("name"), m_names.value(index, QString::number(index + 1))},
                                  {QStringLiteral("peakDbfs"), toDbfs(channel.peak)},
                                  {QStringLiteral("rmsDbfs"), toDbfs(channel.rms)},
                                  {QStringLiteral("displayPeakDbfs"), channel.displayPeakDbfs},
                                  {QStringLiteral("heldPeakDbfs"), channel.heldPeakDbfs},
                                  {QStringLiteral("maxPeakDbfs"), toDbfs(channel.maxPeak)},
                                  {QStringLiteral("clipped"), channel.clipped},
                                  {QStringLiteral("silent"), channel.peak == 0},
                                  {QStringLiteral("invalidSamples"), qlonglong(m_invalidSamples + channel.invalid)}});
    }
    return result;
}

double AudioMeterModel::blockMilliseconds() const
{
    return m_sampleRate > 0 && m_blockFrames > 0 ? 1000.0 * m_blockFrames / m_sampleRate : 0.0;
}

double AudioMeterModel::rmsWindowMilliseconds() const
{
    return m_rmsWindowMilliseconds;
}

int AudioMeterModel::windowBlocks() const
{
    if (m_sampleRate <= 0 || m_blockFrames <= 0) return 1;
    return std::max(1, int(std::lround(m_rmsWindowMilliseconds * m_sampleRate / (1000.0 * m_blockFrames))));
}

QString AudioMeterModel::integrationWindow() const
{
    if (m_blockFrames <= 0)
        return tr("sample peak and RMS per submitted block; hold %1 ms; fall %2 dB/s")
            .arg(number(m_holdMilliseconds), number(m_fallDbPerSecond));
    return tr("sample peak per %1 ms block; RMS over %2 ms; hold %3 ms; fall %4 dB/s")
        .arg(number(blockMilliseconds()), number(windowBlocks() * blockMilliseconds()),
             number(m_holdMilliseconds), number(m_fallDbPerSecond));
}

double AudioMeterModel::measuredSeconds() const
{
    return m_sampleRate > 0 ? double(m_measuredFrames) / m_sampleRate : 0.0;
}

bool AudioMeterModel::fail(const QString &message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit errorChanged();
    }
    return false;
}

void AudioMeterModel::clearError()
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit errorChanged();
    }
}

void AudioMeterModel::setRmsWindowMilliseconds(double milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 1 || milliseconds > 10000) {
        fail(tr("Invalid RMS window"));
        return;
    }
    if (m_rmsWindowMilliseconds == milliseconds) return;
    m_rmsWindowMilliseconds = milliseconds;
    resetWindow();
    clearError();
    emit settingsChanged();
}

void AudioMeterModel::setHoldMilliseconds(double milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0 || milliseconds > 60000) {
        fail(tr("Invalid peak hold duration"));
        return;
    }
    if (m_holdMilliseconds == milliseconds) return;
    m_holdMilliseconds = milliseconds;
    clearError();
    emit settingsChanged();
}

void AudioMeterModel::setFallDbPerSecond(double dbPerSecond)
{
    if (!std::isfinite(dbPerSecond) || dbPerSecond < 0 || dbPerSecond > 120) {
        fail(tr("Invalid meter fall rate"));
        return;
    }
    if (m_fallDbPerSecond == dbPerSecond) return;
    m_fallDbPerSecond = dbPerSecond;
    clearError();
    emit settingsChanged();
}

void AudioMeterModel::resize(int channels)
{
    m_channels.fill(Channel{}, channels);
    m_names = channelNames(m_layout, channels);
    resetWindow();
}

void AudioMeterModel::resetWindow()
{
    const int blocks = windowBlocks();
    m_windowSquares.fill(0.0, qsizetype(blocks) * m_channels.size());
    m_windowFrames.fill(0, blocks);
    m_windowNext = 0;
}

void AudioMeterModel::configure(int sampleRate, int channels, const QString &layout, int blockFrames)
{
    channels = std::clamp(channels, 0, AudioMeterBlock::kMaxChannels);
    sampleRate = channels > 0 ? std::max(0, sampleRate) : 0;
    blockFrames = sampleRate <= 0 ? 0 : blockFrames > 0 ? blockFrames : AudioMeterTap::defaultBlockFrames(sampleRate);
    const QString name = channels > 0 ? layout : QString();
    if (sampleRate == m_sampleRate && channels == m_channels.size() && name == m_layout
        && blockFrames == m_blockFrames)
        return;
    m_sampleRate = sampleRate;
    m_blockFrames = blockFrames;
    m_layout = name;
    resize(channels);
    m_measuredFrames = 0;
    m_droppedBlocks = 0;
    m_invalidSamples = 0;
    emit configurationChanged();
    emit settingsChanged();
    emit levelsChanged();
}

// Peak ballistics shared by both paths: instant attack, a hold of
// holdMilliseconds, then a fall of fallDbPerSecond; a sample at or above
// full scale latches the clip state.
void AudioMeterModel::advance(Channel &channel, double peak, double elapsedMilliseconds) const
{
    const double fall = m_fallDbPerSecond * elapsedMilliseconds / 1000.0;
    const double current = toDbfs(peak);
    channel.displayPeakDbfs = std::max(current, channel.displayPeakDbfs - fall);
    if (current >= channel.heldPeakDbfs) {
        channel.heldPeakDbfs = current;
        channel.holdRemainingMs = m_holdMilliseconds;
    } else if (channel.holdRemainingMs > elapsedMilliseconds) {
        channel.holdRemainingMs -= elapsedMilliseconds;
    } else {
        channel.holdRemainingMs = 0;
        channel.heldPeakDbfs = std::max(current, channel.heldPeakDbfs - fall);
    }
    if (peak >= 1.0) channel.clipped = true;
}

void AudioMeterModel::consume(const AudioMeterBlock &block)
{
    // AGENT-GUARD: a block measured for another layout (drained after a
    // reconfiguration) never mixes into this stream's state.
    const int channels = int(m_channels.size());
    const int blocks = int(m_windowFrames.size());
    if (block.frames <= 0 || block.channels != channels || channels == 0 || m_sampleRate <= 0 || blocks < 1)
        return;
    const int slot = m_windowNext;
    m_windowNext = (m_windowNext + 1) % blocks;
    m_windowFrames[slot] = block.frames;
    qint64 windowFrames = 0;
    for (int frames : std::as_const(m_windowFrames)) windowFrames += frames;
    const double elapsed = 1000.0 * double(block.frames) / double(m_sampleRate);
    for (int c = 0; c < channels; ++c) {
        m_windowSquares[qsizetype(slot) * channels + c] = block.squares[c];
        double squares = 0;
        for (int b = 0; b < blocks; ++b) squares += m_windowSquares.at(qsizetype(b) * channels + c);
        Channel &channel = m_channels[c];
        const double peak = double(block.peak[c]);
        channel.peak = peak;
        channel.rms = windowFrames > 0 ? std::sqrt(squares / double(windowFrames)) : 0.0;
        channel.maxPeak = std::max(channel.maxPeak, peak);
        channel.invalid += block.invalid[c];
        advance(channel, peak, elapsed);
    }
    m_measuredFrames += block.frames;
}

void AudioMeterModel::publish(quint64 newlyDropped)
{
    m_droppedBlocks += qint64(newlyDropped);
    emit levelsChanged();
}

bool AudioMeterModel::update(const QVector<double> &peaks, const QVector<double> &rms,
                             double elapsedMilliseconds, qsizetype invalidSamples)
{
    if (peaks.isEmpty() || peaks.size() > maximumChannels || peaks.size() != rms.size()
        || !std::isfinite(elapsedMilliseconds) || elapsedMilliseconds < 0
        || elapsedMilliseconds > 60000)
        return fail(tr("Invalid meter block or elapsed time"));
    for (int index = 0; index < peaks.size(); ++index)
        if (!std::isfinite(peaks.at(index)) || !std::isfinite(rms.at(index))
            || peaks.at(index) < 0 || rms.at(index) < 0 || rms.at(index) > peaks.at(index))
            return fail(tr("Invalid peak or RMS channel value"));
    if (m_channels.size() != peaks.size()) resize(int(peaks.size()));
    for (int index = 0; index < peaks.size(); ++index) {
        Channel &channel = m_channels[index];
        channel.peak = peaks.at(index);
        channel.rms = rms.at(index);
        channel.maxPeak = std::max(channel.maxPeak, channel.peak);
        advance(channel, channel.peak, elapsedMilliseconds);
    }
    m_invalidSamples = invalidSamples;
    clearError();
    emit levelsChanged();
    return true;
}

bool AudioMeterModel::submitLevels(const QVariantList &peaks, const QVariantList &rms,
                                   double elapsedMilliseconds)
{
    QVector<double> peakValues, rmsValues;
    peakValues.reserve(peaks.size());
    rmsValues.reserve(rms.size());
    for (const QVariant &value : peaks) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok) return fail(tr("Invalid peak value"));
        peakValues.append(number);
    }
    for (const QVariant &value : rms) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok) return fail(tr("Invalid RMS value"));
        rmsValues.append(number);
    }
    return update(peakValues, rmsValues, elapsedMilliseconds, 0);
}

bool AudioMeterModel::submitInterleaved(const QVariantList &samples, int channels,
                                        double elapsedMilliseconds)
{
    if (channels < 1 || channels > maximumChannels || samples.isEmpty()
        || samples.size() > maximumFixtureSamples || samples.size() % channels)
        return fail(tr("Invalid sample block or channel count"));
    QVector<float> values;
    values.reserve(samples.size());
    for (const QVariant &sample : samples) {
        bool ok = false;
        const double number = sample.toDouble(&ok);
        values.append(ok ? float(number) : qQNaN());
    }
    const AudioBlockLevels block = measureInterleaved(
        std::span<const float>(values.constData(), size_t(values.size())), channels);
    return update(block.peaks, block.rms, elapsedMilliseconds, block.invalidSamples);
}

void AudioMeterModel::resetClip()
{
    for (Channel &channel : m_channels) {
        channel.clipped = false;
        channel.maxPeak = 0;
        channel.invalid = 0;
    }
    m_invalidSamples = 0;
    emit levelsChanged();
}

} // namespace QindaTK
