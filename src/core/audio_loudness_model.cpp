// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_loudness_model.h"

#include <QVariantMap>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {

constexpr qsizetype kMaximumFixtureSamples = 20000000;

double maxOf(double held, double value)
{
    if (std::isnan(held)) return value;
    if (std::isnan(value)) return held;
    return std::max(held, value);
}

QString text(double lufs)
{
    if (std::isnan(lufs)) return QStringLiteral("—");
    if (std::isinf(lufs)) return lufs < 0 ? QStringLiteral("−∞") : QStringLiteral("+∞");
    return QString::number(lufs, 'f', 1);
}

QVariantMap metric(const QString &id, const QString &window, double value)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("unit"), QStringLiteral("LUFS")},
            {QStringLiteral("window"), window},
            {QStringLiteral("value"), value}};
}

} // namespace

AudioLoudnessModel::AudioLoudnessModel(QObject *parent)
    : QObject(parent),
      m_binCount(std::size_t(kGateBins), 0),
      m_binEnergy(std::size_t(kGateBins), 0.0)
{
    clear();
}

AudioLoudnessModel::~AudioLoudnessModel() = default;

double AudioLoudnessModel::loudness(double weightedMeanSquare)
{
    return weightedMeanSquare > 0.0 ? -0.691 + 10.0 * std::log10(weightedMeanSquare)
                                    : -std::numeric_limits<double>::infinity();
}

QVector<double> AudioLoudnessModel::channelWeightsFor(const QString &layout, int channels)
{
    if (layout == QLatin1String("5.1") && channels == 6) return {1.0, 1.0, 1.0, 0.0, 1.41, 1.41};
    return QVector<double>(std::max(0, channels), 1.0);
}

QVariantList AudioLoudnessModel::channelWeights() const
{
    QVariantList weights;
    for (double weight : m_weights) weights.append(weight);
    return weights;
}

double AudioLoudnessModel::measuredSeconds() const
{
    return m_rate > 0 ? double(m_measuredFrames) / m_rate : 0.0;
}

QString AudioLoudnessModel::historyMetric() const
{
    return m_historyShortTerm ? QStringLiteral("short-term") : QStringLiteral("momentary");
}

QString AudioLoudnessModel::historyMetricId() const
{
    return m_historyShortTerm ? QStringLiteral("bs1770.short-term") : QStringLiteral("bs1770.momentary");
}

QString AudioLoudnessModel::historyWindow() const
{
    const double step = m_rate > 0 ? 1000.0 * kSubBlockBlocks * m_blockFrames / m_rate : 100.0;
    return tr("%1 sliding, one reading per %2 ms")
        .arg(m_historyShortTerm ? tr("3 s") : tr("400 ms"), QString::number(step, 'g', 6));
}

void AudioLoudnessModel::setHistoryMetric(const QString &metric)
{
    const bool shortTerm = metric == QLatin1String("short-term");
    if (!shortTerm && metric != QLatin1String("momentary")) {
        fail(tr("Unknown loudness history metric"));
        return;
    }
    clearError();
    if (shortTerm == m_historyShortTerm) return;
    m_historyShortTerm = shortTerm;
    // Readings of another metric never share one history.
    m_historyHead = m_historySize = 0;
    m_historyDirty = true;
    emit settingsChanged();
    emit measured();
}

int AudioLoudnessModel::historyCapacity() const
{
    const double perSecond = m_rate > 0 && m_blockFrames > 0 ? double(m_rate) / (kSubBlockBlocks * m_blockFrames) : 10.0;
    return std::max(1, int(std::lround(m_historySeconds * perSecond)));
}

void AudioLoudnessModel::setHistorySeconds(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 1 || seconds > 3600) {
        fail(tr("Invalid loudness history length"));
        return;
    }
    clearError();
    if (seconds == m_historySeconds) return;
    // Keep the newest readings that fit the new length.
    std::vector<std::pair<double, double>> kept;
    for (std::size_t i = 0; i < m_historySize; ++i)
        kept.push_back(m_history[(m_historyHead + i) % m_history.size()]);
    m_historySeconds = seconds;
    m_history.assign(std::size_t(historyCapacity()), {0.0, 0.0});
    const std::size_t keep = std::min(kept.size(), m_history.size());
    for (std::size_t i = 0; i < keep; ++i) m_history[i] = kept[kept.size() - keep + i];
    m_historyHead = 0;
    m_historySize = keep;
    m_historyDirty = true;
    emit settingsChanged();
    emit measured();
}

QVariantList AudioLoudnessModel::history() const
{
    if (m_historyDirty) {
        m_historyDirty = false;
        m_historyList.clear();
        m_historyList.reserve(qsizetype(m_historySize));
        for (std::size_t i = 0; i < m_historySize; ++i) {
            const auto &[seconds, value] = m_history[(m_historyHead + i) % m_history.size()];
            m_historyList.append(QVariantMap{{QStringLiteral("time"), seconds}, {QStringLiteral("value"), value}});
        }
    }
    return m_historyList;
}

QString AudioLoudnessModel::summary() const
{
    if (m_rate <= 0) return tr("No loudness measurement");
    return tr("Momentary %1, short-term %2, integrated %3 LUFS (%4, %5)")
        .arg(text(m_momentary), text(m_shortTerm), text(integratedLufs()), algorithm(),
             m_layout.isEmpty() ? tr("%n channel(s)", nullptr, m_channels) : m_layout);
}

bool AudioLoudnessModel::fail(const QString &message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit errorChanged();
    }
    return false;
}

void AudioLoudnessModel::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit errorChanged();
}

void AudioLoudnessModel::clear()
{
    const double none = std::numeric_limits<double>::quiet_NaN();
    m_subEnergy = 0.0;
    m_subFrames = 0;
    m_subBlocks = 0;
    m_recent.fill(0.0);
    m_recentNext = 0;
    m_subBlockCount = 0;
    std::fill(m_binCount.begin(), m_binCount.end(), 0u);
    std::fill(m_binEnergy.begin(), m_binEnergy.end(), 0.0);
    m_gatedCount = 0;
    m_gatedEnergy = 0.0;
    m_integrated = none;
    m_integratedDirty = false;
    m_momentary = m_shortTerm = m_maxMomentary = m_maxShortTerm = none;
    m_measuredFrames = 0;
    m_droppedBlocks = 0;
    m_history.assign(std::size_t(historyCapacity()), {0.0, 0.0});
    m_historyHead = m_historySize = 0;
    m_historyDirty = true;
    m_fixtureTap.reset();
    m_fresh = true;
}

void AudioLoudnessModel::configure(int sampleRate, int channels, const QString &layout, int blockFrames)
{
    channels = std::clamp(channels, 0, AudioMeterBlock::kMaxChannels);
    sampleRate = channels > 0 ? std::max(0, sampleRate) : 0;
    blockFrames = sampleRate <= 0 ? 0 : blockFrames > 0 ? blockFrames : AudioMeterTap::defaultBlockFrames(sampleRate);
    const QString name = channels > 0 ? layout : QString();
    if (sampleRate == m_rate && channels == m_channels && name == m_layout && blockFrames == m_blockFrames) return;
    m_rate = sampleRate;
    m_channels = channels;
    m_blockFrames = blockFrames;
    m_layout = name;
    m_weights = channelWeightsFor(name, channels);
    clear();
    emit configurationChanged();
    emit settingsChanged();
    emit measured();
}

void AudioLoudnessModel::reset()
{
    clear();
    emit measured();
}

void AudioLoudnessModel::consume(const AudioMeterBlock &block)
{
    // AGENT-GUARD: blocks of another layout never mix into this measurement.
    if (m_rate <= 0 || block.channels != m_channels || block.frames <= 0) return;
    double energy = 0.0;
    for (int c = 0; c < m_channels; ++c) energy += m_weights.at(c) * block.weighted[c];
    m_subEnergy += energy;
    m_subFrames += block.frames;
    m_measuredFrames += block.frames;
    if (++m_subBlocks == kSubBlockBlocks) closeSubBlock();
}

double AudioLoudnessModel::recentMean(int subBlocks) const
{
    double sum = 0.0;
    for (int i = 1; i <= subBlocks; ++i)
        sum += m_recent[std::size_t((m_recentNext - i + kShortTermSubBlocks) % kShortTermSubBlocks)];
    return sum / subBlocks;
}

void AudioLoudnessModel::closeSubBlock()
{
    m_recent[std::size_t(m_recentNext)] = m_subFrames > 0 ? m_subEnergy / double(m_subFrames) : 0.0;
    m_recentNext = (m_recentNext + 1) % kShortTermSubBlocks;
    ++m_subBlockCount;
    m_subEnergy = 0.0;
    m_subFrames = 0;
    m_subBlocks = 0;
    if (m_subBlockCount >= quint64(kMomentarySubBlocks)) {
        // The 400 ms momentary window is also the BS.1770 gating block; one
        // closes every 100 ms, so consecutive gating blocks overlap by 75 %.
        const double block = recentMean(kMomentarySubBlocks);
        m_momentary = loudness(block);
        m_maxMomentary = maxOf(m_maxMomentary, m_momentary);
        gate(block);
    }
    if (m_subBlockCount >= quint64(kShortTermSubBlocks)) {
        m_shortTerm = loudness(recentMean(kShortTermSubBlocks));
        m_maxShortTerm = maxOf(m_maxShortTerm, m_shortTerm);
    }
    const double reading = m_historyShortTerm ? m_shortTerm : m_momentary;
    if (!std::isnan(reading)) appendHistory(measuredSeconds(), reading);
    m_fresh = true;
}

void AudioLoudnessModel::gate(double energy)
{
    const double level = loudness(energy);
    if (!(level > kAbsoluteGateLufs)) return;
    const int bin = std::clamp(int(std::floor((level - kAbsoluteGateLufs) / kGateResolutionLu)), 0, kGateBins - 1);
    ++m_binCount[std::size_t(bin)];
    m_binEnergy[std::size_t(bin)] += energy;
    ++m_gatedCount;
    m_gatedEnergy += energy;
    m_integratedDirty = true;
}

double AudioLoudnessModel::integratedLufs() const
{
    if (!m_integratedDirty) return m_integrated;
    m_integratedDirty = false;
    m_integrated = std::numeric_limits<double>::quiet_NaN();
    if (m_gatedCount == 0) return m_integrated;
    // AGENT-NOTE: BS.1770-4 relative gate. The threshold uses the exact
    // energy mean of every block above the absolute gate; a bin is kept when
    // its centre lies above the threshold, so only blocks within 0.005 LU of
    // it can be classified at bin resolution.
    const double relative = loudness(m_gatedEnergy / double(m_gatedCount)) + kRelativeGateLu;
    double energy = 0.0;
    quint64 count = 0;
    for (int bin = 0; bin < kGateBins; ++bin) {
        if (!m_binCount[std::size_t(bin)]) continue;
        if (kAbsoluteGateLufs + (bin + 0.5) * kGateResolutionLu <= relative) continue;
        energy += m_binEnergy[std::size_t(bin)];
        count += m_binCount[std::size_t(bin)];
    }
    if (count) m_integrated = loudness(energy / double(count));
    return m_integrated;
}

void AudioLoudnessModel::appendHistory(double seconds, double value)
{
    if (m_history.empty()) return;
    const std::size_t capacity = m_history.size();
    if (m_historySize < capacity) {
        m_history[(m_historyHead + m_historySize) % capacity] = {seconds, value};
        ++m_historySize;
    } else {
        m_history[m_historyHead] = {seconds, value};
        m_historyHead = (m_historyHead + 1) % capacity;
    }
    m_historyDirty = true;
}

void AudioLoudnessModel::publish(quint64 newlyDropped)
{
    m_droppedBlocks += qint64(newlyDropped);
    if (!m_fresh && newlyDropped == 0) return;
    m_fresh = false;
    emit measured();
}

QVariantMap AudioLoudnessModel::report() const
{
    const double subBlockMs = m_rate > 0 ? 1000.0 * kSubBlockBlocks * m_blockFrames / m_rate : 0.0;
    QVariantList samples;
    for (const QVariant &entry : history()) samples.append(entry);
    return {{QStringLiteral("schema"), QStringLiteral("qindatk.loudness-report/1")},
            {QStringLiteral("algorithm"), algorithm()},
            {QStringLiteral("unit"), unit()},
            {QStringLiteral("sampleRate"), m_rate},
            {QStringLiteral("channelLayout"), m_layout},
            {QStringLiteral("channelWeights"), channelWeights()},
            {QStringLiteral("blockFrames"), m_blockFrames},
            {QStringLiteral("subBlockMilliseconds"), subBlockMs},
            {QStringLiteral("measuredSeconds"), measuredSeconds()},
            {QStringLiteral("droppedBlocks"), m_droppedBlocks},
            {QStringLiteral("gating"), QVariantMap{{QStringLiteral("absoluteLufs"), kAbsoluteGateLufs},
                                                   {QStringLiteral("relativeLu"), kRelativeGateLu},
                                                   {QStringLiteral("blockMilliseconds"), kMomentarySubBlocks * subBlockMs},
                                                   {QStringLiteral("overlap"), 0.75},
                                                   {QStringLiteral("resolutionLu"), kGateResolutionLu},
                                                   {QStringLiteral("gatedBlocks"), gatedBlocks()}}},
            {QStringLiteral("metrics"), QVariantList{
                 metric(QStringLiteral("bs1770.momentary"), tr("400 ms sliding"), m_momentary),
                 metric(QStringLiteral("bs1770.short-term"), tr("3 s sliding"), m_shortTerm),
                 metric(QStringLiteral("bs1770.integrated"), tr("gated, since reset"), integratedLufs()),
                 metric(QStringLiteral("bs1770.max-momentary"), tr("since reset"), m_maxMomentary),
                 metric(QStringLiteral("bs1770.max-short-term"), tr("since reset"), m_maxShortTerm)}},
            {QStringLiteral("history"), QVariantMap{{QStringLiteral("metricId"), historyMetricId()},
                                                    {QStringLiteral("window"), historyWindow()},
                                                    {QStringLiteral("samples"), samples}}},
            {QStringLiteral("qualification"),
             tr("ITU-R BS.1770-4 algorithm checked with sine reference signals; not a certified meter")}};
}

bool AudioLoudnessModel::submitInterleaved(const QVariantList &samples, int channels, int sampleRate,
                                           const QString &layout)
{
    if (channels < 1 || channels > AudioMeterBlock::kMaxChannels || sampleRate < 8000 || sampleRate > 768000
        || samples.isEmpty() || samples.size() > kMaximumFixtureSamples || samples.size() % channels)
        return fail(tr("Invalid sample block, channel count or sample rate"));
    const QString name = !layout.isEmpty() ? layout
                         : channels == 1 ? QStringLiteral("Mono")
                         : channels == 2 ? QStringLiteral("Stereo") : QString();
    configure(sampleRate, channels, name);
    if (!m_fixtureTap) m_fixtureTap = std::make_unique<AudioMeterTap>(sampleRate, channels, m_blockFrames);
    std::vector<float> values;
    values.reserve(std::size_t(samples.size()));
    for (const QVariant &sample : samples) {
        bool ok = false;
        const double number = sample.toDouble(&ok);
        values.push_back(ok ? float(number) : std::numeric_limits<float>::quiet_NaN());
    }
    const qint64 frames = qint64(values.size()) / channels;
    // Chunks stay well inside the tap's ring, so nothing is dropped here.
    const qint64 chunk = qint64(m_blockFrames) * 256;
    AudioMeterBlock block;
    for (qint64 at = 0; at < frames; at += chunk) {
        m_fixtureTap->process(values.data() + std::size_t(at) * std::size_t(channels), int(std::min(chunk, frames - at)));
        while (m_fixtureTap->pop(block)) consume(block);
    }
    clearError();
    publish();
    return true;
}

} // namespace QindaTK
