// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioPitchSource: worker-thread pitch analysis of a session source, the
// PitchEditor snapshot, and PitchEditor requests as "pitch" source edits
// (D-194…D-197, docs/audio-pitch.md).
#include "audio_pitch_source.h"

#include "audio_pitch_edit.h"

#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <set>

namespace QindaTK {
namespace {

std::optional<qint64> parseSample(const QString &text)
{
    bool ok = false;
    const qint64 value = text.trimmed().toLongLong(&ok);
    return ok ? std::optional<qint64>(value) : std::nullopt;
}

using AnalysisPtr = std::shared_ptr<const AudioPitch::Analysis>;

AnalysisPtr analyseBuffer(const AudioBufferPtr &buffer, int channel, const AudioPitch::Settings &settings,
                          const CancellationToken &cancel)
{
    std::vector<float> mixed;
    const float *mono = buffer->channel(std::max(0, channel));
    if (channel < 0 && buffer->channels > 1) {
        mixed.assign(size_t(buffer->frames), 0.0f);
        const float scale = 1.0f / float(buffer->channels);
        for (int c = 0; c < buffer->channels; ++c)
            for (qint64 i = 0; i < buffer->frames; ++i) mixed[size_t(i)] += buffer->channel(c)[i] * scale;
        mono = mixed.data();
    }
    auto result = AudioPitch::analyse(mono, buffer->frames, buffer->sampleRate, 0, buffer->frames, settings, cancel);
    return result ? std::make_shared<const AudioPitch::Analysis>(std::move(*result)) : nullptr;
}

} // namespace

AudioPitchSource::AudioPitchSource(QObject *parent)
    : AudioAnalysisSource(parent)
{
}

#define QTK_PITCH_SETTER(setter, field, type)          \
    void AudioPitchSource::setter(type value)          \
    {                                                  \
        if (m_settings.field == value) return;         \
        m_settings.field = value;                      \
        emit settingsChanged();                        \
        analysisSettingsChanged();                     \
    }
QTK_PITCH_SETTER(setMinimumHz, minimumHz, double)
QTK_PITCH_SETTER(setMaximumHz, maximumHz, double)
QTK_PITCH_SETTER(setVoicingThreshold, threshold, double)
#undef QTK_PITCH_SETTER

void AudioPitchSource::setCorrectionAmount(double amount)
{
    amount = std::isfinite(amount) ? std::clamp(amount, 0.0, 1.0) : 0.0;
    if (m_amount == amount) return;
    m_amount = amount;
    emit settingsChanged();
}

void AudioPitchSource::setPitchSnapCents(int cents)
{
    cents = std::clamp(cents, 0, 1200);
    if (m_snapCents == cents) return;
    m_snapCents = cents;
    emit settingsChanged();
}

void AudioPitchSource::analysisSettingsChanged()
{
    abandon();
    clearAnalysis();
    emit resultChanged();
    schedule();
}

void AudioPitchSource::settingsTouched()
{
    // A range or column change only re-publishes; a channel change needs a
    // new analysis, and one in flight for the old channel is abandoned.
    if (m_pendingSerial != 0 && m_pendingChannel != channel()) abandon();
    schedule();
}

void AudioPitchSource::clearAnalysis()
{
    m_analysis.reset();
    m_analysisBuffer.reset();
    m_analysisSession = nullptr;
    m_analysisResource.clear();
    m_pendingSerial = 0;
    m_current = false;
    m_holding = false;
    m_snapshot.clear();
}

void AudioPitchSource::invalidate()
{
    AudioSourceEditModel *edits = session() ? session()->edits() : nullptr;
    if (!edits || (m_analysis && (m_analysisSession != session() || m_analysisResource != resourceId()))) {
        clearAnalysis();
        emit resultChanged();
        return;
    }
    m_pendingSerial = 0; // the base abandoned any request in flight
    // Only a preview becoming ready touches a source while previewState is
    // "ready": commit, cancel, apply, undo and relink all leave it first.
    m_holding = edits->previewState() == QLatin1String("ready") && edits->previewResourceId() == resourceId();
    if (!m_holding) {
        const AudioBufferPtr committed = edits->buffer(resourceId());
        if (!(m_analysis && committed == m_analysisBuffer) && m_current) {
            m_current = false;
            if (!m_snapshot.isEmpty()) m_snapshot.insert(QStringLiteral("stale"), true);
        }
    }
    emit resultChanged();
}

void AudioPitchSource::run()
{
    const AudioBufferPtr buffer = currentBuffer();
    if (!buffer || channel() >= buffer->channels) {
        const bool empty = !session() || resourceId().isEmpty();
        clearAnalysis();
        emit resultChanged();
        if (empty) setState(QStringLiteral("empty"));
        else if (!buffer) setState(QStringLiteral("failed"), tr("The source %1 has no decoded samples").arg(resourceId()));
        else setState(QStringLiteral("failed"), tr("The source has no channel %1").arg(channel()));
        return;
    }
    if (const char *problem = AudioPitch::checkSettings(m_settings, buffer->sampleRate)) {
        clearAnalysis();
        emit resultChanged();
        setState(QStringLiteral("failed"), tr(problem));
        return;
    }
    if (m_holding) {
        // A ready preview: keep the committed analysis; never analyse previewed samples.
        if (m_analysis && m_current) publish();
        else setState(QStringLiteral("loading"));
        return;
    }
    if (m_analysis && m_analysisBuffer == buffer && m_analysisChannel == channel()) {
        m_current = true;
        publish();
        return;
    }
    if (m_pendingSerial != 0 && m_pendingSerial == m_serial && m_pendingChannel == channel()) return; // in flight
    const CancellationToken token = restartCancellation();
    const quint64 serial = m_serial;
    const quint64 revision = m_revision;
    const int analysedChannel = channel();
    m_pendingSerial = serial;
    m_pendingChannel = analysedChannel;
    setState(QStringLiteral("loading"));
    auto *watcher = new QFutureWatcher<AnalysisPtr>(this);
    connect(watcher, &QFutureWatcher<AnalysisPtr>::finished, this,
            [this, watcher, serial, revision, buffer, analysedChannel] {
        const AnalysisPtr analysis = watcher->result();
        watcher->deleteLater();
        if (!stillCurrent(serial, revision)) return; // superseded or cancelled: never published
        m_pendingSerial = 0;
        if (!analysis) {
            setState(QStringLiteral("failed"), tr("The pitch analysis could not be computed"));
            return;
        }
        m_analysis = analysis;
        m_analysisBuffer = buffer;
        m_analysisChannel = analysedChannel;
        m_analysisSession = session();
        m_analysisResource = resourceId();
        ++m_runs;
        run();
    });
    // AGENT-NOTE (D-194): one YIN pass over the whole source per committed
    // buffer, on the thread pool with the request's token checked every 32
    // frames; zooming and scrolling only re-publish from it.
    const AudioPitch::Settings settings = m_settings;
    watcher->setFuture(QtConcurrent::run([buffer, analysedChannel, settings, token] {
        return analyseBuffer(buffer, analysedChannel, settings, token);
    }));
}

QString AudioPitchSource::noteId(const AudioPitch::Note &note) const
{
    return QStringLiteral("note-%1").arg(note.start);
}

QString AudioPitchSource::segmentId(const AudioPitch::Segment &segment) const
{
    return QStringLiteral("segment-%1").arg(segment.start);
}

void AudioPitchSource::publish()
{
    const AudioPitch::Analysis &a = *m_analysis;
    const auto [viewStart, viewEnd] = viewRange();
    const double span = double(std::max<qint64>(1, viewEnd - viewStart));
    const auto x = [&](qint64 sample) { return std::clamp(double(sample - viewStart) / span, 0.0, 1.0); };
    // Contour points are thinned to about one per column over the view.
    const qint64 framesInView = std::max<qint64>(1, (viewEnd - viewStart) / a.hop);
    const qint64 stride = std::max<qint64>(1, (framesInView + columnCount() - 1) / columnCount());
    double low = std::numeric_limits<double>::infinity();
    double high = -low;
    QVariantList segments;
    std::set<qint64> boundaries;
    for (const AudioPitch::Segment &segment : a.segments) {
        if (segment.end <= viewStart || segment.start >= viewEnd) continue;
        QVariantList notes;
        for (int n = segment.firstNote; n < segment.firstNote + segment.noteCount; ++n) {
            const AudioPitch::Note &note = a.notes[size_t(n)];
            boundaries.insert(note.start);
            boundaries.insert(note.end);
            if (note.end <= viewStart || note.start >= viewEnd) continue;
            double confidence = 0.0;
            int voiced = 0;
            for (int f = note.firstFrame; f <= note.lastFrame; ++f)
                if (a.frames[size_t(f)].voiced) {
                    confidence += a.frames[size_t(f)].confidence;
                    ++voiced;
                }
            const QString name = AudioPitchEditing::noteName(note.cents);
            notes.push_back(QVariantMap{{QStringLiteral("id"), noteId(note)},
                                        {QStringLiteral("label"), name},
                                        {QStringLiteral("pitchLabel"), AudioPitchEditing::noteName(note.cents, true)},
                                        {QStringLiteral("startSample"), QString::number(note.start)},
                                        {QStringLiteral("endSample"), QString::number(note.end)},
                                        {QStringLiteral("x0"), x(note.start)},
                                        {QStringLiteral("x1"), x(note.end)},
                                        {QStringLiteral("pitchCents"), note.cents},
                                        {QStringLiteral("formantRatio"), 1.0},
                                        {QStringLiteral("confidence"), voiced ? confidence / voiced : 0.0},
                                        {QStringLiteral("voicedFrames"), voiced}});
        }
        QVariantList contour;
        const int firstFrame = a.notes[size_t(segment.firstNote)].firstFrame;
        const int lastFrame = a.notes[size_t(segment.firstNote + segment.noteCount - 1)].lastFrame;
        for (int f = firstFrame; f <= lastFrame; ++f) {
            const AudioPitch::Frame &frame = a.frames[size_t(f)];
            if (frame.centre < viewStart || frame.centre >= viewEnd) continue;
            if (frame.voiced) {
                low = std::min(low, frame.cents);
                high = std::max(high, frame.cents);
            }
            if ((frame.centre / a.hop) % stride != 0) continue;
            contour.push_back(QVariantMap{{QStringLiteral("sample"), QString::number(frame.centre)},
                                          {QStringLiteral("x"), x(frame.centre)},
                                          {QStringLiteral("pitchCents"), frame.cents},
                                          {QStringLiteral("confidence"), frame.confidence},
                                          {QStringLiteral("voiced"), frame.voiced}});
        }
        segments.push_back(QVariantMap{{QStringLiteral("id"), segmentId(segment)},
                                       {QStringLiteral("label"), tr("Phrase at %1 s").arg(double(segment.start) / a.sampleRate, 0, 'f', 2)},
                                       {QStringLiteral("startSample"), QString::number(segment.start)},
                                       {QStringLiteral("endSample"), QString::number(segment.end)},
                                       {QStringLiteral("notes"), notes},
                                       {QStringLiteral("contour"), contour}});
    }
    if (!(high >= low)) {
        low = AudioPitch::hzToCents(m_settings.minimumHz);
        high = AudioPitch::hzToCents(m_settings.maximumHz);
    }
    // Time targets: frame-cell edges, thinned to at most 256, plus every note edge.
    const qint64 edgeStride = std::max<qint64>(1, (framesInView + 255) / 256) * a.hop;
    for (qint64 s = (viewStart / edgeStride) * edgeStride - a.hop / 2; s <= viewEnd; s += edgeStride)
        if (s >= viewStart && s >= 0) boundaries.insert(s);
    QVariantList edges;
    for (const qint64 sample : boundaries)
        if (sample >= viewStart && sample <= viewEnd)
            edges.push_back(QVariantMap{{QStringLiteral("x"), x(sample)}, {QStringLiteral("sample"), QString::number(sample)}});
    m_snapshot = {{QStringLiteral("available"), true},
                  {QStringLiteral("stale"), !m_current},
                  {QStringLiteral("revision"), QString::number(m_revision)},
                  {QStringLiteral("detector"), detector()},
                  {QStringLiteral("corrector"), corrector()},
                  {QStringLiteral("sampleRate"), a.sampleRate},
                  {QStringLiteral("hopSamples"), a.hop},
                  {QStringLiteral("startSample"), QString::number(viewStart)},
                  {QStringLiteral("endSample"), QString::number(viewEnd)},
                  {QStringLiteral("minimumPitchCents"), std::floor((low - 150.0) / 100.0) * 100.0},
                  {QStringLiteral("maximumPitchCents"), std::ceil((high + 150.0) / 100.0) * 100.0},
                  {QStringLiteral("confidenceThreshold"), 1.0 - m_settings.threshold},
                  {QStringLiteral("formantMinimum"), AudioPitch::kMinFormantRatio},
                  {QStringLiteral("formantMaximum"), AudioPitch::kMaxFormantRatio},
                  {QStringLiteral("maximumShiftCents"), AudioPitch::kMaxShiftCents},
                  {QStringLiteral("sampleBoundaries"), edges},
                  {QStringLiteral("segments"), segments}};
    emit resultChanged();
    setState(QStringLiteral("ready"));
}

QVariantMap AudioPitchSource::frameAt(const QString &sample) const
{
    const auto at = parseSample(sample);
    const int index = m_analysis && at ? m_analysis->frameAt(*at) : -1;
    if (index < 0) return {{QStringLiteral("ok"), false}};
    const AudioPitch::Frame &frame = m_analysis->frames[size_t(index)];
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("sample"), QString::number(frame.centre)},
            {QStringLiteral("hz"), frame.hz},
            {QStringLiteral("pitchCents"), frame.cents},
            {QStringLiteral("confidence"), frame.confidence},
            {QStringLiteral("voiced"), frame.voiced}};
}

QVariantMap AudioPitchSource::noteAt(const QString &sample) const
{
    const auto at = parseSample(sample);
    if (!m_analysis || !at) return {{QStringLiteral("ok"), false}};
    for (const AudioPitch::Note &note : m_analysis->notes)
        if (*at >= note.start && *at < note.end)
            return {{QStringLiteral("ok"), true},
                    {QStringLiteral("id"), noteId(note)},
                    {QStringLiteral("segmentId"), segmentId(m_analysis->segments[size_t(note.segment)])},
                    {QStringLiteral("startSample"), QString::number(note.start)},
                    {QStringLiteral("endSample"), QString::number(note.end)},
                    {QStringLiteral("pitchCents"), note.cents},
                    {QStringLiteral("label"), AudioPitchEditing::noteName(note.cents)}};
    return {{QStringLiteral("ok"), false}};
}

QVariantList AudioPitchSource::notes() const
{
    QVariantList list;
    if (!m_analysis) return list;
    for (const AudioPitch::Note &note : m_analysis->notes)
        list.push_back(noteAt(QString::number(note.start)));
    return list;
}

AudioPitchSource::Located AudioPitchSource::locate(const QString &segment, const QString &note) const
{
    Located found;
    if (!m_analysis) return found;
    for (int n = 0; n < int(m_analysis->notes.size()); ++n) {
        const AudioPitch::Note &candidate = m_analysis->notes[size_t(n)];
        const AudioPitch::Segment &owner = m_analysis->segments[size_t(candidate.segment)];
        if (noteId(candidate) == note && segmentId(owner) == segment) return {&candidate, &owner, n};
    }
    return found;
}

} // namespace QindaTK
