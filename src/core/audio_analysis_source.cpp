// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioAnalysisSource (settings, range navigation, request bookkeeping) and
// AudioWaveformSource (peak pyramid columns). AudioSpectrogramSource lives in
// audio_spectrogram_source.cpp.
#include "audio_analysis_source.h"

#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {

std::optional<qint64> parseSample(const QString &text)
{
    bool ok = false;
    const qint64 value = text.trimmed().toLongLong(&ok);
    return ok ? std::optional<qint64>(value) : std::nullopt;
}

} // namespace

AudioAnalysisSource::AudioAnalysisSource(QObject *parent)
    : QObject(parent)
{
}

AudioAnalysisSource::~AudioAnalysisSource()
{
    m_cancel.cancel();
}

void AudioAnalysisSource::setSession(AudioSession *session)
{
    if (m_session == session) return;
    disconnect(m_sourceConnection);
    m_session = session;
    if (session)
        m_sourceConnection = connect(session->edits(), &AudioSourceEditModel::sourceChanged, this,
                                     [this](const QString &id, const QString &) { sourceChanged(id); });
    abandon();
    invalidate();
    emit sessionChanged();
    schedule();
}

void AudioAnalysisSource::setResourceId(const QString &id)
{
    if (m_resourceId == id) return;
    m_resourceId = id;
    abandon();
    invalidate();
    emit resourceIdChanged();
    schedule();
}

void AudioAnalysisSource::setChannel(int channel)
{
    channel = std::max(-1, channel);
    if (m_channel == channel) return;
    m_channel = channel;
    emit channelChanged();
    settingsTouched();
}

void AudioAnalysisSource::setStartSample(const QString &sample)
{
    if (m_startSample == sample) return;
    m_startSample = sample;
    emit rangeChanged();
    settingsTouched();
}

void AudioAnalysisSource::setEndSample(const QString &sample)
{
    if (m_endSample == sample) return;
    m_endSample = sample;
    emit rangeChanged();
    settingsTouched();
}

void AudioAnalysisSource::setColumnCount(int count)
{
    count = std::clamp(count, 1, AudioPeakPyramid::kMaxColumns);
    if (m_columnCount == count) return;
    m_columnCount = count;
    emit columnCountChanged();
    settingsTouched();
}

void AudioAnalysisSource::sourceChanged(const QString &resourceId)
{
    if (resourceId != m_resourceId) return;
    abandon();
    invalidate();
    schedule();
}

void AudioAnalysisSource::schedule()
{
    // "loading" at once, so a reader never mistakes the previous result for
    // the answer to the change it just made.
    if (m_session && !m_resourceId.isEmpty()) setState(QStringLiteral("loading"));
    m_skipRun = false; // a change after cancel() runs again
    if (m_scheduled) return;
    m_scheduled = true;
    QMetaObject::invokeMethod(this, [this] {
        m_scheduled = false;
        if (std::exchange(m_skipRun, false)) return;
        run();
    }, Qt::QueuedConnection);
}

void AudioAnalysisSource::abandon()
{
    m_cancel.cancel();
    ++m_serial;
}

CancellationToken AudioAnalysisSource::restartCancellation()
{
    abandon();
    m_cancel = CancellationSource();
    return m_cancel.token();
}

void AudioAnalysisSource::cancel()
{
    abandon();
    m_skipRun = m_scheduled; // a queued run for the cancelled change never starts
    setState(QStringLiteral("canceled"));
}

void AudioAnalysisSource::setState(const QString &state, const QString &error)
{
    if (m_state == state && m_error == error) return;
    m_state = state;
    m_error = error;
    emit stateChanged();
}

AudioBufferPtr AudioAnalysisSource::currentBuffer()
{
    AudioSourceEditModel *edits = m_session ? m_session->edits() : nullptr;
    const AudioBufferPtr buffer = edits && !m_resourceId.isEmpty() ? edits->buffer(m_resourceId) : nullptr;
    const quint64 revision = edits ? edits->revisionNumber(m_resourceId) : 0;
    const qint64 frames = buffer ? buffer->frames : 0;
    const int rate = buffer ? buffer->sampleRate : 0;
    const int channels = buffer ? buffer->channels : 0;
    if (revision != m_revision || frames != m_frames || rate != m_sampleRate || channels != m_channels) {
        m_revision = revision;
        m_frames = frames;
        m_sampleRate = rate;
        m_channels = channels;
        emit resultChanged();
    }
    return buffer;
}

std::pair<qint64, qint64> AudioAnalysisSource::viewRange() const
{
    const qint64 start = std::clamp<qint64>(parseSample(m_startSample).value_or(0), 0, m_frames);
    const qint64 end = m_endSample.trimmed().isEmpty() ? m_frames
                                                       : std::clamp<qint64>(parseSample(m_endSample).value_or(m_frames),
                                                                            start, m_frames);
    return {start, end};
}

void AudioAnalysisSource::setRange(qint64 start, qint64 end)
{
    const QString first = QString::number(start);
    const QString last = end >= m_frames ? QString() : QString::number(end);
    if (first == m_startSample && last == m_endSample) return;
    m_startSample = first;
    m_endSample = last;
    emit rangeChanged();
    settingsTouched();
}

void AudioAnalysisSource::zoom(const QString &anchorSample, double factor)
{
    if (m_frames <= 0 || !(factor > 0.0) || !std::isfinite(factor)) return;
    const auto [start, end] = viewRange();
    const qint64 length = std::max<qint64>(1, end - start);
    const qint64 shortest = std::min(m_frames, kMinimumZoomSamples);
    const qint64 next = std::clamp<qint64>(std::llround(double(length) / factor), shortest, m_frames);
    const qint64 anchor = std::clamp<qint64>(parseSample(anchorSample).value_or((start + end) / 2), start, end);
    // The anchor keeps its fraction of the range.
    qint64 first = anchor - std::llround(double(anchor - start) * double(next) / double(length));
    first = std::clamp<qint64>(first, 0, m_frames - next);
    setRange(first, first + next);
}

void AudioAnalysisSource::scroll(double fraction)
{
    if (m_frames <= 0 || !std::isfinite(fraction)) return;
    const auto [start, end] = viewRange();
    const qint64 length = end - start;
    const qint64 first = std::clamp<qint64>(start + std::llround(fraction * double(length)), 0, m_frames - length);
    setRange(first, first + length);
}

void AudioAnalysisSource::showAll()
{
    setRange(0, m_frames);
}

// ---- AudioWaveformSource ----

AudioWaveformSource::AudioWaveformSource(QObject *parent)
    : AudioAnalysisSource(parent)
{
}

void AudioWaveformSource::invalidate()
{
    m_pyramid.reset();
    m_pyramidRevision = 0;
    m_buildSerial = 0;
}

void AudioWaveformSource::run()
{
    const AudioBufferPtr buffer = currentBuffer();
    if (!buffer) {
        m_columns.clear();
        m_channelColumns.clear();
        emit resultChanged();
        if (!session() || resourceId().isEmpty()) setState(QStringLiteral("empty"));
        else setState(QStringLiteral("failed"), tr("The source %1 has no decoded samples").arg(resourceId()));
        return;
    }
    if (channel() >= buffer->channels) {
        m_columns.clear();
        m_channelColumns.clear();
        emit resultChanged();
        setState(QStringLiteral("failed"), tr("The source has no channel %1").arg(channel()));
        return;
    }
    if (m_pyramid && m_pyramidRevision == m_revision && m_pyramid->buffer() == buffer) {
        const auto [start, end] = viewRange();
        const auto columnsOf = [&, start = start, end = end](int channelIndex) {
            QVariantList list;
            for (const AudioPeakPyramid::Column &column : m_pyramid->columns(channelIndex, start, end, columnCount()))
                list.push_back(QVariantMap{{QStringLiteral("minimum"), column.minimum},
                                           {QStringLiteral("maximum"), column.maximum},
                                           {QStringLiteral("startSample"), QString::number(column.start)},
                                           {QStringLiteral("endSample"), QString::number(column.end)}});
            return list;
        };
        m_columns = columnsOf(channel());
        m_channelColumns.clear();
        if (m_splitChannels)
            for (int c = 0; c < buffer->channels; ++c) m_channelColumns.push_back(columnsOf(c));
        emit resultChanged();
        setState(QStringLiteral("ready"));
        return;
    }
    if (m_buildSerial != 0 && m_buildSerial == m_serial && m_buildingRevision == m_revision) return; // in flight
    const CancellationToken token = restartCancellation();
    const quint64 serial = m_serial;
    const quint64 revision = m_revision;
    m_buildSerial = serial;
    m_buildingRevision = revision;
    setState(QStringLiteral("loading"));
    auto *watcher = new QFutureWatcher<AudioPeakPyramidPtr>(this);
    connect(watcher, &QFutureWatcher<AudioPeakPyramidPtr>::finished, this, [this, watcher, serial, revision] {
        const AudioPeakPyramidPtr pyramid = watcher->result();
        watcher->deleteLater();
        if (!stillCurrent(serial, revision)) return; // superseded or cancelled: never published
        m_buildSerial = 0;
        if (!pyramid) {
            setState(QStringLiteral("failed"), tr("The peak pyramid could not be built"));
            return;
        }
        m_pyramid = pyramid;
        m_pyramidRevision = revision;
        run();
    });
    // AGENT-NOTE (D-135): one pass over the samples per source revision, off
    // the GUI thread; zooming and scrolling then only read the pyramid.
    watcher->setFuture(QtConcurrent::run([buffer, token] { return AudioPeakPyramid::build(buffer, token); }));
}

void AudioWaveformSource::setSplitChannels(bool split)
{
    if (m_splitChannels == split) return;
    m_splitChannels = split;
    emit splitChannelsChanged();
    settingsTouched();
}

QVariantList AudioWaveformSource::channelLayout() const
{
    QVariantList layout;
    for (int c = 0; c < channels(); ++c) {
        const QString label = channels() == 2 ? (c == 0 ? tr("L") : tr("R")) : QString::number(c + 1);
        layout.push_back(QVariantMap{{QStringLiteral("id"), QString::number(c)}, {QStringLiteral("label"), label}});
    }
    return layout;
}

QVariantMap AudioWaveformSource::extrema(const QString &startSample, const QString &endSample) const
{
    const auto start = parseSample(startSample);
    const auto end = parseSample(endSample);
    if (!m_pyramid || !start || !end || *end <= *start) return {{QStringLiteral("ok"), false}};
    const AudioPeakPyramid::Extrema e = m_pyramid->extrema(channel(), *start, *end);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("minimum"), e.minimum}, {QStringLiteral("maximum"), e.maximum}};
}

} // namespace QindaTK
