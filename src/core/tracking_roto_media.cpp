// SPDX-License-Identifier: LGPL-3.0-or-later
// Decoded-footage input for TrackingRotoModel (docs/tracking-roto.md
// "Tracking footage"): frames come from the WP-070 MediaDecoder on a worker.
#include "tracking_roto_model.h"
#include "media_decoder.h"
#include "tracking_engine.h"

#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace QindaTK {

double TrackingRotoModel::frameLoadProgress() const
{
    if (m_load.total < 1) return 0.0;
    return std::clamp(double(m_load.done) / double(m_load.total), 0.0, 1.0);
}

bool TrackingRotoModel::loadMediaFrames(const QString &uri, int firstFrame, int count)
{
    if (m_load.running || m_analysis.running || uri.isEmpty() || firstFrame < 0
        || count < 1 || count > 300) {
        setError(QStringLiteral("Frame loading needs a media URI, no running job and 1–300 frames."));
        return false;
    }
    // A cancelled load stops at its next frame; never leave it unjoined.
    m_loadFuture.waitForFinished();
    m_load = Load();
    m_load.running = true;
    m_load.total = count;
    m_load.job = ++m_jobSerial;
    const quint64 id = m_load.job;
    const CancellationToken token = m_load.cancel.token();
    // AGENT-NOTE: the decoder lives and dies on the worker (MediaDecoder is
    // single-thread by contract); proxies are made there too, so the GUI
    // thread only stores finished Grayscale8 images. Frames are decoded at
    // their coded size: tracking coordinates are source pixels.
    m_loadFuture = QtConcurrent::run([this, id, token, uri, firstFrame, count] {
        MediaDecoder decoder;
        OperationError error = decoder.open(uri, token);
        const MediaSourceInfoPtr info = error ? MediaSourceInfoPtr() : decoder.info();
        if (!error && (!info || !info->hasVideo()))
            error = {ErrorCode::Unsupported, QStringLiteral("The media has no video frames"), {}, {}};
        if (!error && qint64(firstFrame) + count > info->frameCount())
            error = {ErrorCode::InvalidArgument,
                     QStringLiteral("Frames %1–%2 exceed the media's %3 frames")
                         .arg(firstFrame).arg(firstFrame + count - 1).arg(info->frameCount()), {}, {}};
        for (int i = 0; !error && i < count; ++i) {
            const int frame = firstFrame + i;
            const MediaDecodedFrame decoded = decoder.decodeFrame(frame, QSize(), token);
            if (decoded.error) { error = decoded.error; break; }
            // A concealed (corrupt) frame is never tracked: analysis over it
            // fails as unloaded rather than measuring damaged pixels.
            if (decoded.corrupt) {
                error = {ErrorCode::BackendFailure, QStringLiteral("Frame %1 is corrupt").arg(frame), {}, {}};
                break;
            }
            QString proxyError;
            const QImage gray = TrackingEngine::proxy(decoded.image, &proxyError);
            if (gray.isNull()) { error = {ErrorCode::InvalidArgument, proxyError, {}, {}}; break; }
            QMetaObject::invokeMethod(this, [this, id, frame, gray] { onFrameLoaded(id, frame, gray); },
                                      Qt::QueuedConnection);
        }
        if (token.isCancelled()) return;
        const QString message = error ? error.message : QString();
        QMetaObject::invokeMethod(this, [this, id, message] { onLoadFinished(id, message); },
                                  Qt::QueuedConnection);
    });
    setError({});
    emit loadChanged();
    return true;
}

void TrackingRotoModel::cancelFrameLoad()
{
    if (!m_load.running) return;
    m_load.cancel.cancel();
    m_load.running = false;
    setError(QStringLiteral("Frame loading was cancelled; frames already loaded remain."));
    emit loadChanged();
    emit frameLoadFinished(false, m_error);
}

void TrackingRotoModel::onFrameLoaded(quint64 job, int frame, const QImage &proxy)
{
    if (!m_load.running || m_load.job != job) return;
    if (!storeProxy(frame, proxy)) {
        const QString error = m_error;
        m_load.cancel.cancel();
        m_load.running = false;
        emit loadChanged();
        emit frameLoadFinished(false, error);
        return;
    }
    ++m_load.done;
    emit loadChanged();
}

void TrackingRotoModel::onLoadFinished(quint64 job, const QString &error)
{
    if (!m_load.running || m_load.job != job) return;
    m_load.running = false;
    setError(error);
    emit loadChanged();
    emit frameLoadFinished(error.isEmpty(), error);
}

} // namespace QindaTK
