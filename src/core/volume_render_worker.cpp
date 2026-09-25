// SPDX-License-Identifier: LGPL-3.0-or-later
#include "volume_render_worker.h"

namespace QindaTK {
VolumeRenderWorker::VolumeRenderWorker(QObject *parent) : QObject(parent)
{
    // One thread: an image and a texture pack never compete for the volume
    // cache, and a superseded job stops at its next cancellation check.
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("QindaTK volume renderer"));
}
VolumeRenderWorker::~VolumeRenderWorker() { cancelAndWait(); }

void VolumeRenderWorker::start(const Request &request)
{
    auto &slot = m_cancel[request.kind];
    if (slot) slot->store(true); // the running job of this kind is obsolete
    auto canceled = std::make_shared<std::atomic<bool>>(false);
    slot = canceled;
    ++m_pending;
    m_pool.start([this, request, canceled] {
        Result result;
        result.kind = request.kind;
        result.generation = request.generation;
        result.size = request.size;
        result.voxelType = request.voxelType;
        std::copy(request.shape, request.shape + 3, result.shape);
        const auto stop = [&canceled] { return canceled->load(std::memory_order_relaxed); };
        if (!stop()) {
            if (request.kind == Image) {
                result.image = VolumeRaycaster::render(*request.volume, request.table, request.params,
                                                       request.geometry, request.size, stop);
                result.ok = !result.image.isNull();
            } else {
                result.ok = VolumeRaycaster::packTexture(*request.volume, request.params.channel,
                                                         request.params.frame, &result.bytes, stop);
            }
        }
        result.canceled = stop();
        if (result.canceled) { result.image = {}; result.bytes = {}; result.ok = false; }
        // Queued to the GUI thread; dropped if the worker is gone by then.
        QMetaObject::invokeMethod(this, [this, result] {
            --m_pending;
            if (delivered) delivered(result);
        }, Qt::QueuedConnection);
    });
}
void VolumeRenderWorker::cancel()
{
    for (auto &slot : m_cancel)
        if (slot) slot->store(true);
}
void VolumeRenderWorker::cancelAndWait()
{
    cancel();
    m_pool.waitForDone();
}
} // namespace QindaTK
