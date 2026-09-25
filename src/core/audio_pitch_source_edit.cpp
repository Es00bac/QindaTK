// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioPitchSource requests: PitchEditor gestures as "pitch" source edits
// with previews, and the reference test signal (D-196, D-197,
// docs/audio-pitch.md).
#include "audio_pitch_source.h"

#include "audio_dsp.h"
#include "audio_pitch_edit.h"

#include <QUrl>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {

std::optional<qint64> parseSample(const QString &text)
{
    bool ok = false;
    const qint64 value = text.trimmed().toLongLong(&ok);
    return ok ? std::optional<qint64>(value) : std::nullopt;
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QVariantMap unchangedResult()
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("unchanged"), true}};
}

bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

} // namespace

QVariantMap AudioPitchSource::editFor(const QString &segment, const QString &note, const QString &operation,
                                      const QString &anchorSample, const QString &targetSample,
                                      double targetPitchCents, double targetFormantRatio) const
{
    // AGENT-GUARD: a stale analysis must never become an edit: its note
    // ranges and pitches describe samples that no longer play.
    if (!m_analysis) return failure(tr("No pitch analysis is available"));
    if (!m_current) return failure(tr("The pitch analysis is out of date; wait for the new analysis"));
    const Located found = locate(segment, note);
    if (!found.note) return failure(tr("No note %1 in %2").arg(note, segment));
    const AudioPitch::Note &target = *found.note;
    QVariantMap edit{{QStringLiteral("operation"), QStringLiteral("pitch")},
                     {QStringLiteral("channel"), channel()},
                     {QStringLiteral("startSample"), QString::number(target.start)},
                     {QStringLiteral("endSample"), QString::number(target.end)},
                     {QStringLiteral("minimumHz"), m_settings.minimumHz},
                     {QStringLiteral("maximumHz"), m_settings.maximumHz},
                     {QStringLiteral("voicingThreshold"), m_settings.threshold}};
    if (operation == QLatin1String("pitch")) {
        if (!std::isfinite(targetPitchCents)) return failure(tr("The target pitch must be a number of cents"));
        double cents = targetPitchCents;
        if (m_snapCents > 0) cents = std::round(cents / m_snapCents) * m_snapCents;
        cents = std::clamp(cents, target.cents - AudioPitch::kMaxShiftCents, target.cents + AudioPitch::kMaxShiftCents);
        if (std::fabs(cents - target.cents) < 0.5 && m_amount <= 0.0) return unchangedResult();
        edit.insert(QStringLiteral("targetPitchCents"), cents);
        edit.insert(QStringLiteral("correctionAmount"), m_amount);
    } else if (operation == QLatin1String("formant")) {
        if (!std::isfinite(targetFormantRatio)) return failure(tr("The formant ratio must be a number"));
        const double ratio = std::clamp(targetFormantRatio, AudioPitch::kMinFormantRatio, AudioPitch::kMaxFormantRatio);
        if (std::fabs(ratio - 1.0) < 1e-3) return unchangedResult();
        edit.insert(QStringLiteral("formantRatio"), ratio);
    } else if (operation == QLatin1String("time")) {
        const auto anchor = parseSample(anchorSample);
        const auto wanted = parseSample(targetSample);
        if (!anchor || !wanted) return failure(tr("Sample positions must be whole decimal numbers"));
        if (*anchor != target.start) return failure(tr("A note is retimed by moving its start"));
        // The region: from the previous note's start (or the previous
        // phrase's end), at most one note length back, to this note's end.
        qint64 left = 0;
        if (found.index > found.segment->firstNote) left = m_analysis->notes[size_t(found.index) - 1].start;
        else if (target.segment > 0) left = m_analysis->segments[size_t(target.segment) - 1].end;
        left = std::max(left, target.start - (target.end - target.start));
        const qint64 right = target.end;
        const double before = double(*anchor - left);
        const double after = double(right - *anchor);
        const qint64 lo = std::max(left + qint64(std::ceil(before * AudioPitch::kMinStretch)),
                                   right - qint64(std::floor(after * AudioPitch::kMaxStretch)));
        const qint64 hi = std::min(left + qint64(std::floor(before * AudioPitch::kMaxStretch)),
                                   right - qint64(std::ceil(after * AudioPitch::kMinStretch)));
        if (before <= 0.0 || after <= 0.0 || lo > hi) return failure(tr("This note has no room to move"));
        const qint64 moved = std::clamp(*wanted, lo, hi);
        if (moved == *anchor) return unchangedResult();
        edit.insert(QStringLiteral("startSample"), QString::number(left));
        edit.insert(QStringLiteral("endSample"), QString::number(right));
        edit.insert(QStringLiteral("anchorSample"), QString::number(*anchor));
        edit.insert(QStringLiteral("targetSample"), QString::number(moved));
    } else {
        return failure(tr("Unknown pitch operation \"%1\"").arg(operation));
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("unchanged"), false}, {QStringLiteral("edit"), edit}};
}

QVariantMap AudioPitchSource::previewEdit(const QString &gestureId, const QString &segment, const QString &note,
                                          const QString &operation, const QString &anchorSample,
                                          const QString &targetSample, double targetPitchCents,
                                          double targetFormantRatio)
{
    AudioSourceEditModel *edits = session() ? session()->edits() : nullptr;
    if (!edits) return failure(tr("No session"));
    const QVariantMap made = editFor(segment, note, operation, anchorSample, targetSample, targetPitchCents,
                                     targetFormantRatio);
    if (!ok(made) || made.value(QStringLiteral("unchanged")).toBool()) {
        // What plays always matches the latest request: nothing, here.
        cancelEdit(gestureId);
        return made;
    }
    const QVariantMap edit = made.value(QStringLiteral("edit")).toMap();
    m_previewGesture = gestureId;
    m_previewEdit = edit;
    return edits->preview(gestureId, resourceId(), edit);
}

QVariantMap AudioPitchSource::commitEdit(const QString &gestureId, const QString &segment, const QString &note,
                                         const QString &operation, const QString &anchorSample,
                                         const QString &targetSample, double targetPitchCents,
                                         double targetFormantRatio)
{
    AudioSourceEditModel *edits = session() ? session()->edits() : nullptr;
    if (!edits) return failure(tr("No session"));
    const QVariantMap made = editFor(segment, note, operation, anchorSample, targetSample, targetPitchCents,
                                     targetFormantRatio);
    if (!ok(made) || made.value(QStringLiteral("unchanged")).toBool()) {
        cancelEdit(gestureId);
        return made;
    }
    const QVariantMap edit = made.value(QStringLiteral("edit")).toMap();
    // The ready preview of exactly this request becomes the undo step as it
    // was heard; anything else is computed now.
    if (gestureId == m_previewGesture && edit == m_previewEdit && edits->previewRequestId() == gestureId
        && edits->previewState() == QLatin1String("ready")) {
        m_previewGesture.clear();
        m_previewEdit.clear();
        return edits->commitPreview(gestureId);
    }
    cancelEdit(gestureId);
    return edits->apply(resourceId(), edit);
}

bool AudioPitchSource::cancelEdit(const QString &gestureId)
{
    if (gestureId == m_previewGesture) {
        m_previewGesture.clear();
        m_previewEdit.clear();
    }
    AudioSourceEditModel *edits = session() ? session()->edits() : nullptr;
    return edits && edits->cancelPreview(gestureId);
}

QVariantMap AudioPitchSource::writeTestSignal(const QString &file, const QVariantList &notes, int sampleRate)
{
    const QUrl url(file);
    const QString path = url.isLocalFile() ? url.toLocalFile() : file;
    if (path.isEmpty()) return failure(tr("No file"));
    if (sampleRate < 8000 || sampleRate > 384000) return failure(tr("The sample rate must be 8–384 kHz"));
    AudioBuffer buffer;
    buffer.sampleRate = sampleRate;
    buffer.channels = 1;
    const double maxHz = std::min(5000.0, sampleRate / 2.0 - 1000.0);
    const qint64 fade = std::max<qint64>(1, sampleRate / 1000);
    double phase = 0.0;
    for (const QVariant &value : notes) {
        const QVariantMap spec = value.toMap();
        const double seconds = spec.value(QStringLiteral("seconds")).toDouble();
        if (!(seconds > 0.0) || seconds > 600.0) return failure(tr("Each note needs a length of 0–600 seconds"));
        const qint64 count = std::llround(seconds * sampleRate);
        const QVariant centsValue = spec.value(QStringLiteral("pitchCents"));
        const double cents = centsValue.isValid() && !centsValue.isNull() ? centsValue.toDouble()
                                                                          : std::numeric_limits<double>::quiet_NaN();
        const double vibrato = spec.value(QStringLiteral("vibratoCents")).toDouble();
        const size_t offset = buffer.samples.size();
        buffer.samples.resize(offset + size_t(count), 0.0f);
        if (!std::isfinite(cents)) continue; // silence
        for (qint64 i = 0; i < count; ++i) {
            const double t = double(i) / sampleRate;
            const double hz = AudioPitch::centsToHz(cents + vibrato * std::sin(2.0 * AudioDsp::kPi * 5.5 * t));
            double sample = 0.0;
            for (int h = 1; h * hz < maxHz; ++h) {
                const double f = h * hz;
                const double formant = 0.25 + 1.0 / (1.0 + std::pow((f - 1000.0) / 250.0, 2.0));
                sample += formant / h * std::sin(h * phase);
            }
            const double edge = std::min<double>({1.0, double(i + 1) / fade, double(count - i) / fade});
            buffer.samples[offset + size_t(i)] = float(0.3 * edge * sample);
            phase = std::fmod(phase + 2.0 * AudioDsp::kPi * hz / sampleRate, 2.0 * AudioDsp::kPi);
        }
    }
    buffer.frames = qint64(buffer.samples.size());
    if (buffer.frames == 0) return failure(tr("The signal has no samples"));
    QString error;
    if (!AudioFile::writeWav(path, buffer, &error)) return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}, {QStringLiteral("frames"), QString::number(buffer.frames)}};
}

} // namespace QindaTK
