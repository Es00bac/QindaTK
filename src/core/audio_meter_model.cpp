// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_meter_model.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int maximumChannels = 64;
constexpr qsizetype maximumFixtureSamples = 1000000;
constexpr double floorDbfs = -120;
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
        ? std::max(floorDbfs, 20 * std::log10(linear)) : floorDbfs;
}

QVariantList AudioMeterModel::levels() const
{
    QVariantList result;
    result.reserve(m_channels.size());
    for (int index = 0; index < m_channels.size(); ++index) {
        const Channel &channel = m_channels.at(index);
        result.append(QVariantMap{{QStringLiteral("channel"), index},
                                  {QStringLiteral("peakDbfs"), toDbfs(channel.peak)},
                                  {QStringLiteral("rmsDbfs"), toDbfs(channel.rms)},
                                  {QStringLiteral("displayPeakDbfs"), channel.displayPeakDbfs},
                                  {QStringLiteral("heldPeakDbfs"), channel.heldPeakDbfs},
                                  {QStringLiteral("clipped"), channel.clipped},
                                  {QStringLiteral("silent"), channel.peak == 0},
                                  {QStringLiteral("invalidSamples"), qlonglong(m_invalidSamples)}});
    }
    return result;
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
    if (m_channels.size() != peaks.size()) m_channels.fill(Channel{}, peaks.size());
    const double fall = m_fallDbPerSecond * elapsedMilliseconds / 1000.0;
    for (int index = 0; index < peaks.size(); ++index) {
        Channel &channel = m_channels[index];
        channel.peak = peaks.at(index);
        channel.rms = rms.at(index);
        const double current = toDbfs(channel.peak);
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
        if (channel.peak >= 1.0) channel.clipped = true;
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
    for (Channel &channel : m_channels) channel.clipped = false;
    emit levelsChanged();
}

} // namespace QindaTK
