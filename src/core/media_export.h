// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "job_model.h"
#include "media_encoder.h"
#include "media_program.h"
#include "render_queue_model.h"

#include <QHash>
#include <QImage>
#include <QStringList>
#include <QVariantMap>

namespace QindaTK {

struct MediaExportResult {
    OperationError error;
    qint64 frames = 0;
    qint64 samples = 0;
    QStringList missingResourceIds;
};

// A bars, frame-code and sync-pop test signal (docs/media-playback.md
// "Test signal"). Every frame carries its index as a 16-cell binary code in
// the bottom strip; pop frames (index >= popPhase and (index - popPhase) %
// popInterval == 0) are white above the strip and their exact sample span is a
// 1 kHz tone at half scale; all other audio is digital silence.
struct MediaTestSignalSettings {
    QSize frameSize{320, 180};
    TimelineRate frameRate{25, 1};
    qint64 frames = 50;   // 1..65536
    int sampleRate = 48000;
    int channels = 2;     // 0 writes video only
    qint64 popInterval = 25; // 0 disables pops
    // First pop frame. A nonzero phase keeps frame 0, the frame thumbnails and
    // freshly opened monitors show, on colour bars instead of the white flash.
    qint64 popPhase = 0;
};

// AGENT-CONTRACT (docs/media-playback.md "Export"): export renders the same
// MediaProgram frames and audio mix that playback presents, one frame and
// its exact sample span at a time, so audio written for frame f starts at
// frameStartSample(f). Missing media fails the export; it never writes
// placeholder pictures or silence for an offline clip.
namespace MediaExport {
QString providerId();
// Program frames [firstFrame, firstFrame + frameCount) with their audio.
MediaExportResult exportProgram(const MediaProgram &program, const QString &path, qint64 firstFrame,
                                qint64 frameCount, const CancellationToken &cancel = {},
                                const JobProgress &progress = {});
// RenderQueueModel provider. settings: {program (MediaProgram JSON),
// firstFrame, frameCount, resources: [{resourceId, path, sha256}]}. Every
// source is re-hashed before encoding; a changed or offline source fails.
RenderProduct renderProvider(const QJsonObject &snapshot, const QString &stageDirectory,
                             const CancellationToken &cancel, const JobProgress &progress);
// A queue snapshot for a timeline program. `knownHashes` (resource ID to
// SHA-256 hex) avoids re-hashing files the ResourceResolver already hashed.
QVariantMap queueSnapshot(const MediaProgram &program, const QString &outputPath, const QString &label,
                          const QHash<QString, QString> &knownHashes = {}, QString *error = nullptr);

// media_export_jobs.cpp. Both write a sibling temporary file and rename it
// over `path` only after the encoder finished.
MediaExportResult writeTestSignal(const QString &path, const MediaTestSignalSettings &settings,
                                  const CancellationToken &cancel = {}, const JobProgress &progress = {});
// Reads a test-signal frame code; -1 for a null image.
qint64 readTestSignalCode(const QImage &frame);
// The test-signal picture of one frame (tagged sRGB, Format_RGB32).
QImage testSignalFrame(qint64 frame, const MediaTestSignalSettings &settings);
// A lossless FFV1/PCM proxy of a constant-rate source: same frame count and
// rate, at most maxHeight lines at the source's display aspect.
MediaExportResult writeProxy(const QString &sourceUri, const QString &path, int maxHeight,
                             const CancellationToken &cancel = {}, const JobProgress &progress = {});
} // namespace MediaExport

} // namespace QindaTK
