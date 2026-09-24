// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_decode_worker.h"

#include <algorithm>

namespace QindaTK {
namespace {
void decodeSourceFrame(const MediaWorkerJob &job, MediaDecoderPool &pool, const CancellationToken &token,
                       MediaWorkerResult *result)
{
    MediaDecoder *decoder = pool.acquire(job.uri, token, &result->error);
    if (!decoder) return;
    result->info = decoder->info();
    if (!result->info || !result->info->hasVideo()) {
        result->error = {ErrorCode::Unsupported, QStringLiteral("The media has no video frames"),
                         QStringLiteral("uri"), QStringLiteral("media")};
        return;
    }
    const auto duration = result->info->durationTicks(job.rate);
    if (!duration || *duration < 1) {
        result->error = {ErrorCode::InvalidArgument, QStringLiteral("The source tick rate is invalid"),
                         {}, QStringLiteral("media")};
        return;
    }
    // AGENT-NOTE: a composition layer or graph source holds its first and
    // last frame outside the media, like a still image (docs/media-playback.md).
    const qint64 held = std::clamp<qint64>(job.ticks, 0, *duration - 1);
    const qint64 frame = result->info->frameAtTicks(held, job.rate).value_or(0);
    const MediaDecodedFrame decoded = decoder->decodeFrame(frame, job.outputSize, token);
    result->frame = frame;
    result->error = decoded.error;
    result->rendered.frame = frame;
    result->rendered.image = decoded.image;
    result->rendered.corrupt = decoded.corrupt;
    result->rendered.error = decoded.error;
}
} // namespace

MediaDecodeWorker::MediaDecodeWorker(Completion completion)
    : m_completion(std::move(completion))
{
    // AGENT-GUARD: start the thread last; run() reads every member above.
    m_thread = std::thread([this] { run(); });
}

MediaDecodeWorker::~MediaDecodeWorker()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        for (MediaWorkerJob &job : m_jobs) job.cancel.cancel();
        m_jobs.clear();
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void MediaDecodeWorker::submit(MediaWorkerJob job)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) return;
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
}

int MediaDecodeWorker::queued() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return int(m_jobs.size());
}

void MediaDecodeWorker::run()
{
    MediaDecoderPool pool;
    while (true) {
        MediaWorkerJob job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
            if (m_stopping) return;
            // min_element returns the first of equal priorities: FIFO within a priority.
            const auto next = std::min_element(m_jobs.begin(), m_jobs.end(),
                [](const MediaWorkerJob &a, const MediaWorkerJob &b) { return a.priority < b.priority; });
            job = std::move(*next);
            m_jobs.erase(next);
        }
        MediaWorkerResult result;
        result.kind = job.kind;
        result.serial = job.serial;
        result.frame = job.frame;
        result.program = job.program;
        result.uri = job.uri;
        const CancellationToken token = job.cancel.token();
        if (token.isCancelled()) {
            result.cancelled = true;
        } else if (job.kind == MediaWorkerJob::Kind::Probe) {
            MediaDecoder *decoder = pool.acquire(job.uri, token, &result.error);
            if (decoder) result.info = decoder->info();
            result.cancelled = result.error.code == ErrorCode::Cancelled;
        } else if (job.kind == MediaWorkerJob::Kind::SourceFrame) {
            decodeSourceFrame(job, pool, token, &result);
            result.cancelled = result.error.code == ErrorCode::Cancelled;
        } else if (job.program) {
            result.rendered = job.program->render(job.frame, job.outputSize, pool, token);
            result.error = result.rendered.error;
            result.cancelled = result.error.code == ErrorCode::Cancelled;
        } else {
            result.error = {ErrorCode::InvalidArgument, QStringLiteral("A render job needs a program"),
                            {}, QStringLiteral("media")};
        }
        m_completion(std::move(result));
    }
}

} // namespace QindaTK
