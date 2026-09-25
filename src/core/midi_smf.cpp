// SPDX-License-Identifier: LGPL-3.0-or-later
// Standard MIDI File import/export (WP-064, docs/audio-midi.md "Interchange")
// and the MidiSequenceModel invokables that use it.
#include "midi_smf.h"

#include "midi_sequence_model.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

namespace QindaTK {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("MidiFile", text);
}

constexpr qint64 kMaximumFileBytes = qint64(64) * 1024 * 1024;
constexpr qint64 kMaximumFileTick = qint64(1) << 40;

void vlq(QByteArray &out, quint64 value)
{
    char buffer[10];
    int count = 0;
    buffer[count++] = char(value & 0x7F);
    while (value >>= 7) buffer[count++] = char(0x80 | (value & 0x7F));
    while (count) out.append(buffer[--count]);
}

void be(QByteArray &out, quint32 value, int bytes)
{
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) out.append(char((value >> shift) & 0xFF));
}

QByteArray chunk(const char *id, const QByteArray &data)
{
    QByteArray out(id, 4);
    be(out, quint32(data.size()), 4);
    return out + data;
}

// One outgoing event; at one tick note-offs go first, note-ons last, so
// back-to-back notes of one pitch pair correctly on import.
struct Out {
    qint64 tick = 0;
    int order = 1;
    QByteArray bytes;
};

QByteArray track(QVector<Out> events, qint64 end)
{
    std::stable_sort(events.begin(), events.end(),
                     [](const Out &a, const Out &b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
    QByteArray data;
    qint64 at = 0;
    for (const Out &event : events) {
        vlq(data, quint64(event.tick - at));
        data += event.bytes;
        at = event.tick;
    }
    vlq(data, quint64(std::max<qint64>(0, end - at)));
    data += QByteArray("\xFF\x2F\x00", 3);
    return chunk("MTrk", data);
}

QByteArray message(int a, int b, int c = -1)
{
    QByteArray bytes;
    bytes.append(char(a));
    bytes.append(char(b));
    if (c >= 0) bytes.append(char(c));
    return bytes;
}

qint64 roundDiv(qint64 numerator, qint64 denominator)
{
    qint64 quotient = numerator / denominator;
    const qint64 remainder = numerator % denominator;
    if (2 * (remainder < 0 ? -remainder : remainder) >= denominator) quotient += numerator < 0 ? -1 : 1;
    return quotient;
}

class Reader {
public:
    Reader(const QByteArray &data, qsizetype from, qsizetype to) : m_data(data), m_at(from), m_end(to) {}
    bool ok() const { return m_ok; }
    bool atEnd() const { return m_at >= m_end; }
    quint8 byte()
    {
        if (m_at >= m_end) {
            m_ok = false;
            return 0;
        }
        return quint8(m_data.at(m_at++));
    }
    quint32 be(int bytes)
    {
        quint32 value = 0;
        for (int i = 0; i < bytes; ++i) value = (value << 8) | byte();
        return value;
    }
    quint64 vlq()
    {
        quint64 value = 0;
        for (int i = 0; i < 4; ++i) { // SMF quantities are at most four bytes
            const quint8 next = byte();
            value = (value << 7) | (next & 0x7F);
            if (!(next & 0x80)) return value;
        }
        m_ok = false;
        return value;
    }
    QByteArray bytes(quint64 count)
    {
        if (count > quint64(m_end - m_at)) {
            m_ok = false;
            m_at = m_end;
            return {};
        }
        const QByteArray out = m_data.mid(m_at, qsizetype(count));
        m_at += qsizetype(count);
        return out;
    }
    qsizetype position() const { return m_at; }

private:
    const QByteArray &m_data;
    qsizetype m_at = 0;
    qsizetype m_end = 0;
    bool m_ok = true;
};

QString localPath(const QString &file)
{
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

} // namespace

QByteArray MidiFile::write(const MidiPattern &pattern, int ppq, const QVector<TempoEvent> &tempo, ExportReport *report)
{
    ExportReport local;
    ExportReport &r = report ? *report : local;
    r = {};
    QVector<Out> conductor;
    for (const TempoEvent &event : tempo) {
        if (event.kind == TempoEvent::Kind::Tempo) {
            QByteArray bytes("\xFF\x51\x03", 3);
            be(bytes, quint32(std::clamp<qint64>(event.microsecondsPerQuarter, 1, 0xFFFFFF)), 3);
            conductor.push_back({event.tick, 1, bytes});
        } else {
            int power = 0;
            while ((1 << power) < event.denominator && power < 7) ++power;
            QByteArray bytes("\xFF\x58\x04", 3);
            bytes.append(char(event.numerator)).append(char(power)).append(char(24)).append(char(8));
            conductor.push_back({event.tick, 1, bytes});
        }
    }
    QVector<Out> events;
    QByteArray name("\xFF\x03", 2);
    const QByteArray text = pattern.name.toUtf8();
    vlq(name, quint64(text.size()));
    events.push_back({0, -1, name + text});
    qint64 end = pattern.length;
    QHash<QUuid, int> pitches;
    for (const MidiNote &note : pattern.notes) {
        const int velocity = Midi::velocityTo7(note.velocity);
        if (Midi::velocityFrom7(velocity) != note.velocity) ++r.reducedVelocities;
        events.push_back({note.tick, 2, message(0x90 | note.channel, note.pitch, velocity)});
        events.push_back({note.end(), 0, message(0x80 | note.channel, note.pitch, 0x40)});
        end = std::max(end, note.end());
        pitches.insert(note.id, note.pitch);
    }
    for (const MidiControlEvent &event : pattern.events) {
        const int bits = Midi::resolutionBits(event.kind) == 14 ? 14 : 7;
        const quint32 value = Midi::scaleDown(event.value, 32, bits);
        if (event.kind != MidiController::NoteBend && Midi::scaleUp(value, bits, 32) != event.value) ++r.reducedValues;
        switch (event.kind) {
        case MidiController::ControlChange:
            events.push_back({event.tick, 1, message(0xB0 | event.channel, event.number, int(value))});
            break;
        case MidiController::PitchBend:
            events.push_back({event.tick, 1, message(0xE0 | event.channel, int(value & 0x7F), int(value >> 7))});
            break;
        case MidiController::ChannelPressure:
            events.push_back({event.tick, 1, message(0xD0 | event.channel, int(value))});
            break;
        case MidiController::NotePressure: // MIDI 1.0 polyphonic key pressure on the note's key
            // After the note-on at the same tick, so an importer finds the sounding key.
            events.push_back({event.tick, 3, message(0xA0 | event.channel, pitches.value(event.noteId), int(value))});
            break;
        case MidiController::NoteBend:
            ++r.droppedNoteBends;
            break;
        }
    }
    for (const MidiStepRow &row : pattern.rows)
        for (const MidiStep &step : row.steps) r.droppedSteps += step.enabled ? 1 : 0;
    // Nested notes of one key: FIFO pairing on import gives the ends back in start order.
    for (qsizetype i = 0; i < pattern.notes.size(); ++i)
        for (qsizetype j = i + 1; j < pattern.notes.size(); ++j) {
            const MidiNote &a = pattern.notes.at(i);
            const MidiNote &b = pattern.notes.at(j);
            if (a.pitch == b.pitch && a.channel == b.channel && b.tick < a.end() && b.end() < a.end()) {
                ++r.ambiguousOverlaps;
                break;
            }
        }
    QByteArray header;
    be(header, 1, 2);
    be(header, 2, 2);
    be(header, quint32(ppq), 2);
    qint64 conductorEnd = 0;
    for (const Out &event : conductor) conductorEnd = std::max(conductorEnd, event.tick);
    return chunk("MThd", header) + track(conductor, conductorEnd) + track(events, end);
}

std::optional<MidiFile::Imported> MidiFile::read(const QByteArray &data, int ppq, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<Imported> {
        if (error) *error = message;
        return std::nullopt;
    };
    Reader file(data, 0, data.size());
    if (file.bytes(4) != "MThd") return fail(tr("This is not a Standard MIDI File"));
    const quint32 headerLength = file.be(4);
    Imported result;
    result.format = int(file.be(2));
    const int tracks = int(file.be(2));
    const quint32 division = file.be(2);
    file.bytes(headerLength > 6 ? headerLength - 6 : 0);
    if (!file.ok() || headerLength < 6) return fail(tr("The MIDI file header is truncated"));
    if (result.format > 1) return fail(tr("MIDI file format 2 (independent sequences) is not supported"));
    if (division & 0x8000) return fail(tr("SMPTE time division is not supported"));
    if (division == 0) return fail(tr("The MIDI file has no time division"));
    result.division = int(division);
    const auto rescale = [&](qint64 tick) {
        if ((tick * ppq) % qint64(division) != 0) ++result.roundedPositions;
        return roundDiv(tick * ppq, qint64(division));
    };
    struct Open { qint64 start = 0; quint16 velocity = 1; QUuid id; };
    for (int index = 0; index < tracks && !file.atEnd(); ++index) {
        const QByteArray id = file.bytes(4);
        const quint32 length = file.be(4);
        if (!file.ok() || length > quint32(data.size() - file.position())) return fail(tr("A MIDI track is truncated"));
        const qsizetype from = file.position();
        file.bytes(length);
        if (id != "MTrk") {
            --index; // unknown chunks are skipped and do not count as tracks
            continue;
        }
        Reader in(data, from, from + qsizetype(length));
        MidiPattern pattern;
        pattern.id = QUuid::createUuid();
        QHash<int, QList<Open>> open; // channel * 128 + key
        QVector<std::pair<qint64, qint64>> spans; // file ticks of each note: start, end
        qint64 tick = 0;
        qint64 end = -1;
        quint8 status = 0;
        while (!in.atEnd() && end < 0) {
            tick += qint64(in.vlq());
            if (tick > kMaximumFileTick) return fail(tr("A MIDI track is longer than 2^40 ticks"));
            quint8 lead = in.byte();
            if (lead == 0xFF) {
                const quint8 type = in.byte();
                const QByteArray payload = in.bytes(in.vlq());
                status = 0; // meta events cancel running status
                if (type == 0x2F) end = tick;
                else if (type == 0x51 && payload.size() == 3)
                    result.tempo.push_back({QUuid::createUuid(), TempoEvent::Kind::Tempo, tick,
                                            qint64((quint8(payload[0]) << 16) | (quint8(payload[1]) << 8) | quint8(payload[2])),
                                            4, 4});
                else if (type == 0x58 && payload.size() >= 2)
                    result.tempo.push_back({QUuid::createUuid(), TempoEvent::Kind::Meter, tick, 500000,
                                            quint8(payload[0]), 1 << std::min<int>(quint8(payload[1]), 6)});
                else if (type == 0x03 && pattern.name.isEmpty()) pattern.name = QString::fromUtf8(payload);
                continue;
            }
            if (lead == 0xF0 || lead == 0xF7) {
                in.bytes(in.vlq());
                status = 0;
                ++result.skippedEvents;
                continue;
            }
            quint8 first = lead;
            if (lead & 0x80) {
                status = lead;
                first = in.byte();
            } else if (!status) {
                return fail(tr("A MIDI track uses running status without a status byte"));
            }
            const int type = status & 0xF0;
            const int channel = status & 0x0F;
            const int second = type == 0xC0 || type == 0xD0 ? 0 : in.byte();
            const int key = channel * 128 + (first & 0x7F);
            MidiControlEvent event;
            event.id = QUuid::createUuid();
            event.tick = tick;
            event.channel = channel;
            if (type == 0x90 && second > 0) {
                open[key].push_back({tick, Midi::velocityFrom7(second), QUuid::createUuid()});
                continue;
            }
            if (type == 0x80 || type == 0x90) {
                if (open[key].isEmpty()) continue; // a note-off without a note
                const Open note = open[key].takeFirst();
                pattern.notes.push_back({note.id, note.start, 1, first & 0x7F, channel, note.velocity});
                spans.push_back({note.start, tick});
                continue;
            }
            if (type == 0xA0 && !open[key].isEmpty()) {
                event.kind = MidiController::NotePressure;
                event.noteId = open[key].first().id;
                event.value = Midi::scaleUp(quint32(second), 7, 32);
            } else if (type == 0xB0) {
                event.kind = MidiController::ControlChange;
                event.number = first & 0x7F;
                event.value = Midi::scaleUp(quint32(second), 7, 32);
            } else if (type == 0xE0) {
                event.kind = MidiController::PitchBend;
                event.value = Midi::scaleUp(quint32((first & 0x7F) | ((second & 0x7F) << 7)), 14, 32);
            } else if (type == 0xD0) {
                event.kind = MidiController::ChannelPressure;
                event.value = Midi::scaleUp(quint32(first & 0x7F), 7, 32);
            } else {
                ++result.skippedEvents; // program change, pressure without a sounding note
                continue;
            }
            pattern.events.push_back(event);
        }
        if (!in.ok()) return fail(tr("A MIDI track is truncated"));
        if (end < 0) end = tick;
        for (auto it = open.cbegin(); it != open.cend(); ++it)
            for (const Open &note : it.value()) { // still sounding at the end of the track
                pattern.notes.push_back({note.id, note.start, 1, it.key() % 128, it.key() / 128, note.velocity});
                spans.push_back({note.start, std::max(end, note.start + 1)});
                ++result.unterminatedNotes;
            }
        if (pattern.notes.isEmpty() && pattern.events.isEmpty()) continue;
        qint64 last = 0;
        for (qsizetype n = 0; n < pattern.notes.size(); ++n) {
            MidiNote &note = pattern.notes[n];
            note.tick = rescale(spans.at(n).first);
            note.duration = std::max<qint64>(1, rescale(spans.at(n).second) - note.tick);
            last = std::max(last, note.tick);
        }
        for (MidiControlEvent &event : pattern.events) {
            event.tick = rescale(event.tick);
            last = std::max(last, event.tick);
        }
        pattern.length = std::max(rescale(end), last + 1);
        pattern.seed = quint32(Midi::key(pattern.id));
        if (pattern.name.isEmpty()) pattern.name = tr("Track %1").arg(index + 1);
        Midi::sortPattern(pattern);
        if (const QString problem = Midi::validate(pattern); !problem.isEmpty()) return fail(problem);
        result.patterns.push_back(pattern);
    }
    for (TempoEvent &event : result.tempo) event.tick = rescale(event.tick);
    return result;
}

QVariantMap MidiSequenceModel::exportMidiFile(const QString &patternId, const QString &file,
                                              const QVariantList &tempo) const
{
    const auto refuse = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    const MidiPattern *source = pattern(QUuid::fromString(patternId));
    if (!source) return refuse(tr("There is no pattern %1").arg(patternId));
    if (m_ppq > 0x7FFF) return refuse(tr("The session PPQ does not fit a MIDI file division"));
    QVector<TempoEvent> events;
    for (const QVariant &value : tempo) {
        const QVariantMap row = value.toMap();
        TempoEvent event;
        event.kind = row.value(QStringLiteral("kind")).toString() == QLatin1String("meter") ? TempoEvent::Kind::Meter
                                                                                            : TempoEvent::Kind::Tempo;
        event.tick = row.value(QStringLiteral("tick")).toString().toLongLong();
        event.microsecondsPerQuarter = row.value(QStringLiteral("microsecondsPerQuarter")).toString().toLongLong();
        event.numerator = row.value(QStringLiteral("numerator"), 4).toInt();
        event.denominator = row.value(QStringLiteral("denominator"), 4).toInt();
        events.push_back(event);
    }
    MidiFile::ExportReport report;
    const QByteArray bytes = MidiFile::write(*source, m_ppq, events, &report);
    const QString path = localPath(file);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        return refuse(tr("The MIDI file could not be written: %1").arg(out.errorString()));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}, {QStringLiteral("bytes"), int(bytes.size())},
            {QStringLiteral("reducedVelocities"), report.reducedVelocities},
            {QStringLiteral("reducedValues"), report.reducedValues},
            {QStringLiteral("droppedNoteBends"), report.droppedNoteBends},
            {QStringLiteral("droppedSteps"), report.droppedSteps},
            {QStringLiteral("ambiguousOverlaps"), report.ambiguousOverlaps}};
}

QVariantMap MidiSequenceModel::importMidiFile(const QString &file)
{
    clearError();
    QFile in(localPath(file));
    if (!in.open(QIODevice::ReadOnly)) return failure(tr("The MIDI file cannot be opened: %1").arg(in.errorString()));
    if (in.size() > kMaximumFileBytes) return failure(tr("The MIDI file is too large"));
    QString error;
    const auto imported = MidiFile::read(in.readAll(), m_ppq, &error);
    if (!imported) return failure(error);
    QStringList ids;
    for (const MidiPattern &pattern : imported->patterns) ids.push_back(Midi::idText(pattern.id));
    QVariantList tempo;
    for (const TempoEvent &event : imported->tempo)
        tempo.push_back(QVariantMap{{QStringLiteral("kind"), event.kind == TempoEvent::Kind::Tempo ? QStringLiteral("tempo")
                                                                                                  : QStringLiteral("meter")},
                                    {QStringLiteral("tick"), QString::number(event.tick)},
                                    {QStringLiteral("microsecondsPerQuarter"), QString::number(event.microsecondsPerQuarter)},
                                    {QStringLiteral("numerator"), event.numerator},
                                    {QStringLiteral("denominator"), event.denominator}});
    return editContent(tr("Import MIDI file"), [&](MidiContent &content) {
        content.patterns += imported->patterns;
        return QString();
    }, {{QStringLiteral("patternIds"), ids}, {QStringLiteral("tempo"), tempo},
        {QStringLiteral("format"), imported->format}, {QStringLiteral("division"), imported->division},
        {QStringLiteral("roundedPositions"), imported->roundedPositions},
        {QStringLiteral("unterminatedNotes"), imported->unterminatedNotes},
        {QStringLiteral("skippedEvents"), imported->skippedEvents}});
}

} // namespace QindaTK
