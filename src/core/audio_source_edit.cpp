// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_source_edit.h"

#include "audio_dsp.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace QindaTK {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AudioSourceEdit", text);
}

struct KindName {
    AudioSourceEdit::Kind kind;
    const char *name;
};
constexpr KindName kKinds[] = {
    {AudioSourceEdit::Kind::Gain, "gain"},       {AudioSourceEdit::Kind::FadeIn, "fadeIn"},
    {AudioSourceEdit::Kind::FadeOut, "fadeOut"}, {AudioSourceEdit::Kind::Silence, "silence"},
    {AudioSourceEdit::Kind::Delete, "delete"},   {AudioSourceEdit::Kind::Crop, "crop"},
    {AudioSourceEdit::Kind::SpectralGain, "spectralGain"},
};
constexpr double kMaxGainDb = 48.0;

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

std::optional<int> channelValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) return -1;
    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text == QLatin1String("all")) return -1;
    bool ok = false;
    const int channel = text.toInt(&ok);
    return ok && channel >= -1 ? std::optional<int>(channel) : std::nullopt;
}

bool inPlace(AudioSourceEdit::Kind kind)
{
    return kind != AudioSourceEdit::Kind::Delete && kind != AudioSourceEdit::Kind::Crop;
}

} // namespace

QString AudioSourceEdit::operation() const
{
    for (const KindName &entry : kKinds)
        if (entry.kind == kind) return QString::fromLatin1(entry.name);
    return {};
}

QString AudioSourceEdit::label() const
{
    switch (kind) {
    case Kind::Gain: return tr("Gain %1 dB").arg(gainDb, 0, 'f', 1);
    case Kind::FadeIn: return tr("Fade in");
    case Kind::FadeOut: return tr("Fade out");
    case Kind::Silence: return tr("Silence");
    case Kind::Delete: return tr("Delete %1 samples").arg(end - start);
    case Kind::Crop: return tr("Crop to %1 samples").arg(end - start);
    case Kind::SpectralGain:
        return gainDb <= AudioDsp::kSilenceDb ? tr("Remove %1–%2 Hz").arg(qRound(lowHz)).arg(qRound(highHz))
                                              : tr("Spectral gain %1 dB, %2–%3 Hz")
                                                    .arg(gainDb, 0, 'f', 1).arg(qRound(lowHz)).arg(qRound(highHz));
    }
    return {};
}

QVariantMap AudioSourceEdit::toMap() const
{
    QVariantMap map{{QStringLiteral("operation"), operation()},
                    {QStringLiteral("channel"), channel},
                    {QStringLiteral("startSample"), QString::number(start)},
                    {QStringLiteral("endSample"), QString::number(end)}};
    if (kind == Kind::Gain || kind == Kind::SpectralGain) map.insert(QStringLiteral("gainDb"), gainDb);
    if (kind == Kind::FadeIn || kind == Kind::FadeOut) map.insert(QStringLiteral("shape"), shape);
    if (kind == Kind::SpectralGain) {
        map.insert(QStringLiteral("lowHz"), lowHz);
        map.insert(QStringLiteral("highHz"), highHz);
        map.insert(QStringLiteral("fftSize"), fftSize);
    }
    return map;
}

std::optional<AudioSourceEdit> AudioSourceEdit::fromMap(const QVariantMap &map, QString *error)
{
    const auto failed = [error](const QString &message) {
        if (error) *error = message;
        return std::nullopt;
    };
    AudioSourceEdit edit;
    const QString name = map.value(QStringLiteral("operation")).toString();
    bool known = false;
    for (const KindName &entry : kKinds)
        if (name == QLatin1String(entry.name)) {
            edit.kind = entry.kind;
            known = true;
        }
    if (!known) return failed(tr("Unknown audio edit \"%1\"").arg(name));
    const auto channel = channelValue(map.value(QStringLiteral("channel")));
    const auto start = sampleValue(map.value(QStringLiteral("startSample")));
    const auto end = sampleValue(map.value(QStringLiteral("endSample")));
    if (!channel) return failed(tr("The channel must be an index or \"all\""));
    if (!start || !end) return failed(tr("Sample positions must be whole decimal numbers"));
    edit.channel = *channel;
    edit.start = *start;
    edit.end = *end;
    const double defaultGain = edit.kind == Kind::SpectralGain ? AudioDsp::kSilenceDb : 0.0;
    edit.gainDb = map.contains(QStringLiteral("gainDb")) ? map.value(QStringLiteral("gainDb")).toDouble() : defaultGain;
    if (!std::isfinite(edit.gainDb)) edit.gainDb = AudioDsp::kSilenceDb; // JSON cannot carry −inf
    edit.gainDb = std::max(edit.gainDb, AudioDsp::kSilenceDb);
    edit.shape = map.value(QStringLiteral("shape"), QStringLiteral("linear")).toString();
    edit.lowHz = map.value(QStringLiteral("lowHz")).toDouble();
    edit.highHz = map.value(QStringLiteral("highHz")).toDouble();
    edit.fftSize = map.value(QStringLiteral("fftSize"), 2048).toInt();
    return edit;
}

double AudioSourceEditing::fadeInGain(qint64 i, qint64 n, const QString &shape)
{
    const double x = n > 0 ? double(i) / double(n) : 1.0;
    return shape == QLatin1String("equalPower") ? std::sin(AudioDsp::kPi / 2.0 * x) : x;
}

QVariantList AudioSourceEditing::capabilities(const AudioBuffer &buffer)
{
    QVariantList list;
    for (const KindName &entry : kKinds) {
        const bool length = !inPlace(entry.kind);
        QStringList parameters;
        if (entry.kind == AudioSourceEdit::Kind::Gain) parameters << QStringLiteral("gainDb");
        if (entry.kind == AudioSourceEdit::Kind::FadeIn || entry.kind == AudioSourceEdit::Kind::FadeOut)
            parameters << QStringLiteral("shape");
        if (entry.kind == AudioSourceEdit::Kind::SpectralGain)
            parameters << QStringLiteral("gainDb") << QStringLiteral("lowHz") << QStringLiteral("highHz")
                       << QStringLiteral("fftSize");
        list.push_back(QVariantMap{{QStringLiteral("operation"), QString::fromLatin1(entry.name)},
                                   {QStringLiteral("perChannel"), !length && buffer.channels > 1},
                                   {QStringLiteral("changesLength"), length},
                                   {QStringLiteral("parameters"), parameters}});
    }
    return list;
}

QString AudioSourceEditing::validate(const AudioBuffer &buffer, const AudioSourceEdit &edit)
{
    if (buffer.frames <= 0 || buffer.channels <= 0) return tr("The source has no samples");
    if (edit.start < 0 || edit.end <= edit.start || edit.end > buffer.frames)
        return tr("The range %1–%2 is not inside the source's %3 samples").arg(edit.start).arg(edit.end).arg(buffer.frames);
    if (edit.channel < -1 || edit.channel >= buffer.channels) return tr("The source has no channel %1").arg(edit.channel);
    if (edit.changesLength() && edit.channel != -1) return tr("Delete and crop apply to every channel");
    if (edit.kind == AudioSourceEdit::Kind::Delete && edit.end - edit.start >= buffer.frames)
        return tr("Deleting every sample would leave an empty source");
    if (!std::isfinite(edit.gainDb) || edit.gainDb > kMaxGainDb) return tr("The gain must be at most +48 dB");
    if ((edit.kind == AudioSourceEdit::Kind::FadeIn || edit.kind == AudioSourceEdit::Kind::FadeOut)
        && edit.shape != QLatin1String("linear") && edit.shape != QLatin1String("equalPower"))
        return tr("A fade shape is \"linear\" or \"equalPower\"");
    if (edit.kind == AudioSourceEdit::Kind::SpectralGain) {
        if (!AudioDsp::validFftSize(edit.fftSize)) return tr("The FFT size must be a power of two from 64 to 16384");
        if (!(edit.lowHz >= 0.0) || !(edit.highHz >= edit.lowHz) || edit.lowHz > buffer.sampleRate / 2.0)
            return tr("The band must rise from 0 Hz up to at most half the sample rate");
        const AudioDsp::SpectralGainSpec spec{edit.start, edit.end, edit.lowHz, edit.highHz, 0.0, edit.fftSize,
                                              buffer.sampleRate};
        if (!AudioDsp::spectralPatchRange(spec, buffer.frames))
            return tr("The selection is shorter than one analysis hop (%1 samples)").arg(edit.fftSize / 4);
    }
    return {};
}

bool AudioSourceEditing::prepare(const AudioBuffer &buffer, AudioSourceEdit &edit, const CancellationToken &cancel,
                                 QString *error)
{
    if (const QString problem = validate(buffer, edit); !problem.isEmpty()) {
        if (error) *error = problem;
        return false;
    }
    if (!inPlace(edit.kind)) return true;
    const int first = edit.channel < 0 ? 0 : edit.channel;
    const int count = edit.channel < 0 ? buffer.channels : 1;
    auto patch = std::make_shared<AudioBuffer>();
    patch->sampleRate = buffer.sampleRate;
    patch->channels = count;
    if (edit.kind == AudioSourceEdit::Kind::SpectralGain) {
        AudioDsp::SpectralGainSpec spec{edit.start, edit.end, edit.lowHz, edit.highHz,
                                        AudioDsp::gainFromDecibels(edit.gainDb), edit.fftSize, buffer.sampleRate};
        for (int c = 0; c < count; ++c) {
            const auto result = AudioDsp::spectralGain(buffer.channel(first + c), buffer.frames, spec, cancel);
            if (!result) {
                if (error) *error = tr("Spectral processing was cancelled");
                return false;
            }
            if (c == 0) {
                edit.patchStart = result->start;
                patch->frames = qint64(result->samples.size());
                patch->samples.resize(size_t(patch->frames) * size_t(count));
            }
            std::copy(result->samples.begin(), result->samples.end(), patch->channel(c));
        }
        edit.patch = std::move(patch);
        return true;
    }
    edit.patchStart = edit.start;
    patch->frames = edit.end - edit.start;
    patch->samples.resize(size_t(patch->frames) * size_t(count));
    const double gain = AudioDsp::gainFromDecibels(edit.gainDb);
    for (int c = 0; c < count; ++c) {
        const float *in = buffer.channel(first + c) + edit.start;
        float *out = patch->channel(c);
        for (qint64 i = 0; i < patch->frames; ++i) {
            if ((i & 0xffff) == 0 && cancel.isCancelled()) {
                if (error) *error = tr("The edit was cancelled");
                return false;
            }
            double factor = 1.0;
            switch (edit.kind) {
            case AudioSourceEdit::Kind::Gain: factor = gain; break;
            case AudioSourceEdit::Kind::FadeIn: factor = fadeInGain(i, patch->frames, edit.shape); break;
            case AudioSourceEdit::Kind::FadeOut: factor = fadeInGain(patch->frames - 1 - i, patch->frames, edit.shape); break;
            case AudioSourceEdit::Kind::Silence: factor = 0.0; break;
            default: break;
            }
            out[i] = float(double(in[i]) * factor);
        }
    }
    edit.patch = std::move(patch);
    return true;
}

AudioBufferPtr AudioSourceEditing::apply(const AudioBuffer &buffer, const AudioSourceEdit &edit)
{
    auto out = std::make_shared<AudioBuffer>();
    out->sampleRate = buffer.sampleRate;
    out->channels = buffer.channels;
    if (inPlace(edit.kind)) {
        *out = buffer;
        if (!edit.patch) return out;
        const int first = edit.channel < 0 ? 0 : edit.channel;
        for (int c = 0; c < edit.patch->channels; ++c)
            std::copy_n(edit.patch->channel(c), edit.patch->frames, out->channel(first + c) + edit.patchStart);
        return out;
    }
    const bool crop = edit.kind == AudioSourceEdit::Kind::Crop;
    out->frames = crop ? edit.end - edit.start : buffer.frames - (edit.end - edit.start);
    out->samples.resize(size_t(out->frames) * size_t(out->channels));
    for (int c = 0; c < buffer.channels; ++c) {
        const float *in = buffer.channel(c);
        float *to = out->channel(c);
        if (crop) {
            std::copy(in + edit.start, in + edit.end, to);
        } else {
            to = std::copy(in, in + edit.start, to);
            std::copy(in + edit.end, in + buffer.frames, to);
        }
    }
    return out;
}

} // namespace QindaTK
