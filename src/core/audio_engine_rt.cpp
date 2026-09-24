// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_engine_p.h"

#include <QDir>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <vector>

namespace QindaTK {

AudioEngineProcessor::AudioEngineProcessor(const AudioStreamFormat &format, const EngineTransportState &initial,
                                           double captureSeconds)
    : m_format(format),
      m_capture(std::max<std::size_t>(1, std::size_t(double(format.sampleRate) * captureSeconds)
                                             * std::size_t(std::max(1, format.inputChannels)))),
      m_scratch(std::make_unique<double[]>(AudioRenderGraph::kMaxBlock)),
      m_position(initial.position),
      m_loopEnabled(initial.loopEnabled),
      m_loopStart(initial.loopStart),
      m_loopEnd(initial.loopEnd)
{
    m_publishedPosition.store(m_position);
    if (format.outputChannels > 0 && format.sampleRate > 0) {
        m_outputTap = std::make_unique<AudioMeterTap>(format.sampleRate, format.outputChannels);
        m_channelMeters = std::make_unique<AudioChannelMeterSink>(format.sampleRate);
    }
}

const AudioRenderGraph *AudioEngineProcessor::offer(const AudioRenderGraph *graph)
{
    return m_pending.exchange(graph, std::memory_order_acq_rel);
}

void AudioEngineProcessor::closeAfterStop()
{
    if (m_recording) endTake();
    m_recording = false;
    m_playing = false;
    publish();
}

EngineTransportState AudioEngineProcessor::stateAfterStop() const
{
    return {m_position, m_loopEnabled, m_loopStart, m_loopEnd};
}

void AudioEngineProcessor::process(const float *input, float *output, int frames) noexcept
{
    if (const AudioRenderGraph *next = m_pending.exchange(nullptr, std::memory_order_acq_rel)) {
        m_graph = next;
        m_adopted.store(next->generation(), std::memory_order_release);
    }
    EngineCommand command;
    for (std::size_t n = 0; n < kCommandCapacity && m_commands.pop(command); ++n) apply(command);

    const int channels = m_format.outputChannels;
    std::fill_n(output, std::size_t(frames) * std::size_t(channels), 0.0f);
    const qint64 blockPosition = m_position;
    const bool render = m_graph && channels > 0 && m_graph->sampleRate() == m_format.sampleRate;
    AudioChannelMeterSink *meters = render ? m_channelMeters.get() : nullptr;
    for (int done = 0; m_playing && done < frames;) {
        int count = frames - done;
        const bool looping = m_loopEnabled && m_loopEnd > m_loopStart && m_position < m_loopEnd;
        if (looping) count = int(std::min<qint64>(count, m_loopEnd - m_position));
        if (render)
            m_graph->render(m_position, count, output + std::size_t(done) * channels, channels, m_scratch.get(), meters);
        m_position += count;
        done += count;
        if (looping && m_position == m_loopEnd) m_position = m_loopStart;
    }
    // Stopped (or nothing to render): the channel meters measure silence, so
    // they fall on audio time exactly like the output meter (D-113).
    if (m_channelMeters && !(m_playing && render))
        m_channelMeters->silence(m_graph ? m_graph->generation() : 0, m_graph ? m_graph->meterSlots() : 0, frames);
    // AGENT-GUARD: the meter measures exactly what the device receives, every
    // period (silence included); AudioMeterTap::process never allocates.
    if (m_outputTap) m_outputTap->process(output, frames);
    if (m_recording) captureBlock(input, frames, blockPosition);
    publish();
}

void AudioEngineProcessor::deviceFailed(const char *reason) noexcept
{
    if (m_recording) endTake(true);
    m_recording = false;
    m_playing = false;
    publish();
    EngineEvent event;
    event.kind = EngineEvent::DeviceLost;
    event.reason = reason;
    m_events.push(event);
}

void AudioEngineProcessor::apply(const EngineCommand &command) noexcept
{
    switch (command.kind) {
    case EngineCommand::Play:
        m_playing = true;
        return ack(command, true);
    case EngineCommand::Stop:
        if (m_recording) endTake();
        m_recording = false;
        m_playing = false;
        return ack(command, true);
    case EngineCommand::Seek:
        if (command.position < 0) return ack(command, false, EngineEvent::InvalidPosition);
        m_position = command.position;
        return ack(command, true);
    case EngineCommand::Loop:
        if (command.enabled && (command.rangeStart < 0 || command.rangeEnd <= command.rangeStart))
            return ack(command, false, EngineEvent::InvalidRange);
        m_loopEnabled = command.enabled;
        if (command.enabled) {
            m_loopStart = command.rangeStart;
            m_loopEnd = command.rangeEnd;
        }
        return ack(command, true);
    case EngineCommand::Record:
        if (!command.enabled) {
            if (m_recording) endTake();
            m_recording = false;
            return ack(command, true);
        }
        if (m_recording) return ack(command, true);
        if (m_format.inputChannels < 1) return ack(command, false, EngineEvent::NoInput);
        if (m_writtenSerial.load(std::memory_order_acquire) != m_takeSerial.load(std::memory_order_relaxed))
            return ack(command, false, EngineEvent::TakeBusy);
        beginTake();
        m_recording = true;
        m_playing = true;
        return ack(command, true);
    default:
        return ack(command, false, EngineEvent::InvalidRange);
    }
}

void AudioEngineProcessor::ack(const EngineCommand &command, bool accepted, int code) noexcept
{
    EngineEvent event;
    event.kind = EngineEvent::Ack;
    event.serial = command.serial;
    event.accepted = accepted;
    event.code = code;
    event.position = m_position;
    m_events.push(event); // capacity exceeds the GUI's in-flight command limit
}

void AudioEngineProcessor::beginTake() noexcept
{
    m_takeAnnounced = false;
    m_takeFrames = 0;
    m_takeDropped = 0;
    m_takeEndTotal.store(kOpenTake, std::memory_order_relaxed);
    m_takeSerial.fetch_add(1, std::memory_order_release);
}

void AudioEngineProcessor::endTake(bool lost) noexcept
{
    if (!m_takeAnnounced) captureBlock(nullptr, 0, m_position);
    m_takeEndTotal.store(m_pushed, std::memory_order_release);
    EngineEvent event;
    event.kind = EngineEvent::RecordEnd;
    event.serial = m_takeSerial.load(std::memory_order_relaxed);
    event.code = lost ? EngineEvent::Lost : EngineEvent::None;
    event.frames = m_takeFrames;
    event.dropped = m_takeDropped;
    m_events.push(event);
}

void AudioEngineProcessor::captureBlock(const float *input, int frames, qint64 position) noexcept
{
    if (!m_takeAnnounced) {
        EngineEvent event;
        event.kind = EngineEvent::RecordBegin;
        event.serial = m_takeSerial.load(std::memory_order_relaxed);
        event.position = position;
        m_events.push(event);
        m_takeAnnounced = true;
    }
    if (frames <= 0) return;
    const std::size_t samples = std::size_t(frames) * std::size_t(m_format.inputChannels);
    if (m_capture.pushSpan(input, samples)) {
        m_pushed += quint64(frames);
        m_takeFrames += frames;
        m_pushedPublished.store(m_pushed, std::memory_order_release);
    } else {
        m_takeDropped += frames;
    }
}

void AudioEngineProcessor::publish() noexcept
{
    m_publishedPosition.store(m_position, std::memory_order_release);
    m_publishedPlaying.store(m_playing, std::memory_order_release);
    m_publishedRecording.store(m_recording, std::memory_order_release);
}

// ---- take writer -----------------------------------------------------------

AudioTakeWriter::AudioTakeWriter(AudioEngineProcessor &processor, std::function<QString()> directory,
                                 Finished finished)
    : m_processor(processor), m_directory(std::move(directory)), m_finished(std::move(finished))
{
    m_thread = std::thread([this] { run(); });
}

void AudioTakeWriter::stop()
{
    m_stop.store(true, std::memory_order_release);
    if (m_thread.joinable()) m_thread.join();
}

void AudioTakeWriter::run()
{
    const int channels = std::max(1, m_processor.format().inputChannels);
    std::vector<float> block(std::size_t(4096) * channels);
    AudioWavWriter file;
    QString path, error;
    quint64 active = 0, finished = 0, consumed = 0;
    for (;;) {
        const bool stopping = m_stop.load(std::memory_order_acquire);
        const quint64 serial = m_processor.takeSerial();
        if (!active && serial != finished) {
            active = serial;
            error.clear();
            const QString directory = m_directory();
            QDir().mkpath(directory);
            path = QDir(directory).filePath(QStringLiteral("take-%1.wav")
                                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
            file.open(path, m_processor.format().sampleRate, channels, &error);
        }
        if (active) {
            const quint64 end = m_processor.takeEnd();
            const quint64 limit = std::min(end, m_processor.pushedFrames());
            while (consumed < limit) {
                const auto count = std::min<quint64>(4096, limit - consumed);
                m_processor.capture().popSpan(block.data(), std::size_t(count) * channels);
                if (file.isOpen()) file.write(block.data(), qint64(count));
                consumed += count;
            }
            if (end != AudioEngineProcessor::kOpenTake && consumed == end) {
                const qint64 frames = file.frames();
                if (file.isOpen() && !file.finish(&error) && error.isEmpty())
                    error = QStringLiteral("The take could not be completed");
                m_finished(active, path, frames, error);
                m_processor.markWritten(active);
                finished = active;
                active = 0;
                continue;
            }
        }
        if (stopping && !active) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace QindaTK
