// SPDX-License-Identifier: LGPL-3.0-or-later
// The "pitch" source edit (D-197, docs/audio-pitch.md): parameters, labels,
// validation and the TD-PSOLA patch for AudioSourceEditModel.
#include "audio_pitch_edit.h"

#include "audio_source_edit.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <optional>

namespace QindaTK::AudioPitchEditing {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AudioPitchEdit", text);
}

std::optional<qint64> sampleValue(const QVariant &value)
{
    bool ok = false;
    qint64 parsed = 0;
    if (value.typeId() == QMetaType::QString) parsed = value.toString().trimmed().toLongLong(&ok);
    else if (value.canConvert<double>()) {
        const double d = value.toDouble(&ok);
        ok = ok && std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15;
        parsed = qint64(d);
    }
    return ok ? std::optional<qint64>(parsed) : std::nullopt;
}

bool present(const QVariantMap &map, const QString &key)
{
    const QVariant value = map.value(key);
    return value.isValid() && !value.isNull() && !(value.typeId() == QMetaType::QString && value.toString().isEmpty());
}

AudioPitch::Correction correction(const AudioSourceEdit &edit)
{
    AudioPitch::Correction c = edit.pitch;
    c.start = edit.start;
    c.end = edit.end;
    return c;
}

} // namespace

QString noteName(double cents, bool withDeviation)
{
    if (!std::isfinite(cents)) return {};
    static const char *const kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const qint64 midi = std::llround(cents / 100.0);
    const int pitchClass = int(((midi % 12) + 12) % 12);
    const qint64 octave = (midi - pitchClass) / 12 - 1;
    QString name = QString::fromLatin1(kNames[pitchClass]) + QString::number(octave);
    const qint64 deviation = std::llround(cents - double(midi) * 100.0);
    if (withDeviation && deviation != 0)
        name += QStringLiteral(" %1%2 ct").arg(deviation > 0 ? QStringLiteral("+") : QString()).arg(deviation);
    return name;
}

bool readMap(const QVariantMap &map, AudioSourceEdit &edit, QString *error)
{
    const auto failed = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    AudioPitch::Correction &c = edit.pitch;
    c = AudioPitch::Correction{};
    if (present(map, QStringLiteral("targetPitchCents"))) {
        bool ok = false;
        c.targetCents = map.value(QStringLiteral("targetPitchCents")).toDouble(&ok);
        if (!ok || !std::isfinite(c.targetCents)) return failed(tr("The target pitch must be a number of cents"));
    }
    bool ok = true;
    if (present(map, QStringLiteral("correctionAmount")))
        c.amount = map.value(QStringLiteral("correctionAmount")).toDouble(&ok);
    if (!ok) return failed(tr("The correction amount must be a number from 0 to 1"));
    if (present(map, QStringLiteral("formantRatio")))
        c.formantRatio = map.value(QStringLiteral("formantRatio")).toDouble(&ok);
    if (!ok) return failed(tr("The formant ratio must be a number"));
    const bool anchor = present(map, QStringLiteral("anchorSample"));
    const bool target = present(map, QStringLiteral("targetSample"));
    if (anchor != target) return failed(tr("Retiming needs both an anchor and a target sample"));
    if (anchor) {
        const auto a = sampleValue(map.value(QStringLiteral("anchorSample")));
        const auto t = sampleValue(map.value(QStringLiteral("targetSample")));
        if (!a || !t) return failed(tr("Sample positions must be whole decimal numbers"));
        c.anchor = *a;
        c.target = *t;
    }
    if (present(map, QStringLiteral("minimumHz"))) c.analysis.minimumHz = map.value(QStringLiteral("minimumHz")).toDouble();
    if (present(map, QStringLiteral("maximumHz"))) c.analysis.maximumHz = map.value(QStringLiteral("maximumHz")).toDouble();
    if (present(map, QStringLiteral("voicingThreshold")))
        c.analysis.threshold = map.value(QStringLiteral("voicingThreshold")).toDouble();
    return true;
}

void writeMap(const AudioSourceEdit &edit, QVariantMap &map)
{
    const AudioPitch::Correction &c = edit.pitch;
    if (std::isfinite(c.targetCents)) map.insert(QStringLiteral("targetPitchCents"), c.targetCents);
    map.insert(QStringLiteral("correctionAmount"), c.amount);
    map.insert(QStringLiteral("formantRatio"), c.formantRatio);
    if (c.anchor >= 0 || c.target >= 0) {
        map.insert(QStringLiteral("anchorSample"), QString::number(c.anchor));
        map.insert(QStringLiteral("targetSample"), QString::number(c.target));
    }
    map.insert(QStringLiteral("minimumHz"), c.analysis.minimumHz);
    map.insert(QStringLiteral("maximumHz"), c.analysis.maximumHz);
    map.insert(QStringLiteral("voicingThreshold"), c.analysis.threshold);
}

QStringList parameters()
{
    return {QStringLiteral("targetPitchCents"), QStringLiteral("correctionAmount"), QStringLiteral("formantRatio"),
            QStringLiteral("anchorSample"),     QStringLiteral("targetSample"),     QStringLiteral("minimumHz"),
            QStringLiteral("maximumHz"),        QStringLiteral("voicingThreshold")};
}

QString label(const AudioSourceEdit &edit)
{
    const AudioPitch::Correction &c = edit.pitch;
    QStringList parts;
    if (std::isfinite(c.targetCents)) {
        parts << (c.amount > 0.0 ? tr("Correct pitch to %1 (%2%)").arg(noteName(c.targetCents, true)).arg(qRound(c.amount * 100.0))
                                 : tr("Pitch to %1").arg(noteName(c.targetCents, true)));
    }
    if (std::fabs(c.formantRatio - 1.0) > 1e-9) parts << tr("Formant ×%1").arg(c.formantRatio, 0, 'f', 2);
    if (c.anchor >= 0 && c.anchor != c.target) parts << tr("Move sample %1 to %2").arg(c.anchor).arg(c.target);
    return parts.isEmpty() ? tr("Pitch edit") : parts.join(QStringLiteral(", "));
}

QString problemText(AudioPitch::Problem problem)
{
    switch (problem) {
    case AudioPitch::Problem::None: return {};
    case AudioPitch::Problem::Range: return tr("A pitch edit needs a range of at least 20 ms inside the source");
    case AudioPitch::Problem::Settings: return tr("The pitch analysis settings do not suit this source");
    case AudioPitch::Problem::Shift:
        return tr("The pitch change must stay within ±%1 cents and the correction amount within 0–1")
            .arg(qRound(AudioPitch::kMaxShiftCents));
    case AudioPitch::Problem::Formant:
        return tr("The formant ratio must lie between %1 and %2")
            .arg(AudioPitch::kMinFormantRatio, 0, 'f', 2).arg(AudioPitch::kMaxFormantRatio, 0, 'f', 2);
    case AudioPitch::Problem::Time:
        return tr("A retimed point must stay inside the range and stretch each side by %1× to %2×")
            .arg(AudioPitch::kMinStretch, 0, 'f', 2).arg(AudioPitch::kMaxStretch, 0, 'f', 2);
    case AudioPitch::Problem::Unvoiced: return tr("The range has no voiced (pitched) material to correct");
    case AudioPitch::Problem::Nothing: return tr("The pitch edit changes nothing");
    case AudioPitch::Problem::Cancelled: return tr("The pitch edit was cancelled");
    }
    return {};
}

QString validate(const AudioBuffer &buffer, const AudioSourceEdit &edit)
{
    return problemText(AudioPitch::check(correction(edit), buffer.frames, buffer.sampleRate));
}

bool prepare(const AudioBuffer &buffer, AudioSourceEdit &edit, const CancellationToken &cancel, QString *error)
{
    const int first = edit.channel < 0 ? 0 : edit.channel;
    const int count = edit.channel < 0 ? buffer.channels : 1;
    std::vector<const float *> channels;
    for (int c = 0; c < count; ++c) channels.push_back(buffer.channel(first + c));
    AudioPitch::Problem problem = AudioPitch::Problem::None;
    const auto result = AudioPitch::correct(channels, buffer.frames, buffer.sampleRate, correction(edit), cancel, &problem);
    if (!result) {
        if (error) *error = problemText(problem);
        return false;
    }
    auto patch = std::make_shared<AudioBuffer>();
    patch->sampleRate = buffer.sampleRate;
    patch->channels = count;
    patch->frames = edit.end - edit.start;
    patch->samples.resize(size_t(patch->frames) * size_t(count));
    for (int c = 0; c < count; ++c)
        std::copy(result->channels[size_t(c)].begin(), result->channels[size_t(c)].end(), patch->channel(c));
    edit.patchStart = result->start;
    edit.patch = std::move(patch);
    return true;
}

} // namespace QindaTK::AudioPitchEditing
