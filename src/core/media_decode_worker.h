// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_program.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace QindaTK {

struct MediaWorkerJob {
    // Render: one program frame. Probe: open and index `uri`. SourceFrame:
    // the frame of `uri` showing at `ticks` (at `rate` ticks per second),
    // held to the first/last frame outside the media.
    enum class Kind { Render, Probe, SourceFrame };
    Kind kind = Kind::Render;
    quint64 serial = 0;
    int priority = 0; // lower runs first: 0 = presentation, 1 = prefetch
    MediaProgramPtr program;
    qint64 frame = -1;
    QSize outputSize;
    QString uri; // Probe, SourceFrame
    qint64 ticks = 0;
    TimelineRate rate{TimelineTime::kDefaultTicksPerSecond, 1};
    CancellationSource cancel;
};

struct MediaWorkerResult {
    MediaWorkerJob::Kind kind = MediaWorkerJob::Kind::Render;
    quint64 serial = 0;
    qint64 frame = -1;
    MediaProgramPtr program;
    QString uri;
    MediaRenderedFrame rendered;
    MediaSourceInfoPtr info;
    OperationError error;
    bool cancelled = false; // dropped before it ran, or stopped by its token
};

// AGENT-CONTRACT (docs/media-playback.md "Frame lifecycle", D-098): one
// decode thread per playback engine owns its MediaDecoderPool, so decoders
// never cross threads. Jobs run lowest priority value first, then in
// submission order. A cancelled job is reported (cancelled = true) without
// running, or stops at the decoder's next packet. The completion runs on
// the worker thread and must only hand the result off to its owner.
class MediaDecodeWorker {
public:
    using Completion = std::function<void(MediaWorkerResult)>;
    explicit MediaDecodeWorker(Completion completion);
    // Cancels queued work, waits for the running job and joins the thread.
    // The owner cancels running jobs' tokens first to keep this short.
    ~MediaDecodeWorker();
    MediaDecodeWorker(const MediaDecodeWorker &) = delete;
    MediaDecodeWorker &operator=(const MediaDecodeWorker &) = delete;

    void submit(MediaWorkerJob job);
    int queued() const;

private:
    void run();
    Completion m_completion;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<MediaWorkerJob> m_jobs;
    bool m_stopping = false;
    std::thread m_thread;
};

} // namespace QindaTK
