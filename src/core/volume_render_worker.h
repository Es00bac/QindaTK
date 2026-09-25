// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "volume_raycast.h"
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <memory>

namespace QindaTK {
// Background volume work for VolumeViewItem (D-261): the reference image and
// the GPU texture pack run on a private one-thread pool instead of the
// scene-graph render thread, each job with its own cancellation flag.
// AGENT-CONTRACT (docs/volume-imaging.md#large-data-and-cancellation):
// - start() cancels the in-flight job of the same kind (it is obsolete);
//   the item compares each result's generation with the one it wants and
//   rejects the rest, so an obsolete frame is never shown.
// - A job only calls VoxelVolume's locked reads (sampleValue, copyChunk)
//   and uses geometry, table and sizes captured at start(); the volume must
//   outlive it, so owners call cancelAndWait() before the volume goes away
//   (VoxelVolume::aboutToBeDestroyed, a volume switch, their destructor).
// - Results are delivered on the worker's (GUI) thread through `delivered`.
class VolumeRenderWorker : public QObject {
public:
    enum Kind { Image = 0, Texture = 1 };
    struct Request {
        Kind kind = Image;
        quint64 generation = 0;
        const VoxelVolume *volume = nullptr;
        VolumeTransferTable table;
        VolumeRayParams params;
        VolumeRayGeometry geometry;
        QSize size;          // Image: the reference size
        QString voxelType;   // Texture: captured volume shape and type
        int shape[3] = {0, 0, 0};
    };
    struct Result {
        Kind kind = Image;
        quint64 generation = 0;
        bool ok = false;       // Texture: false when a chunk was missing
        bool canceled = false; // abandoned before completion
        QSize size;
        QImage image;
        QByteArray bytes;
        QString voxelType;
        int shape[3] = {0, 0, 0};
    };
    explicit VolumeRenderWorker(QObject *parent = nullptr);
    ~VolumeRenderWorker() override;
    std::function<void(const Result &)> delivered;
    void start(const Request &request);
    // Cancels whatever runs (both kinds); results still arrive, canceled.
    void cancel();
    void cancelAndWait();
    // Jobs started whose result has not been delivered yet.
    int pending() const { return m_pending; }
private:
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel[2];
    int m_pending = 0;
};
} // namespace QindaTK
