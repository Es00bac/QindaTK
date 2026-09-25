// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caption_track_model.h"
#include "caption_track_codec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace {
bool decimalMilliseconds(const QString &text, qint64 *value)
{
    static const QRegularExpression digits(QStringLiteral("^(0|[1-9][0-9]*)$"));
    if (!digits.match(text).hasMatch()) return false;
    bool ok = false;
    *value = text.toLongLong(&ok);
    return ok;
}

void sortCues(QVector<CaptionCueRecord> *cues)
{
    std::stable_sort(cues->begin(), cues->end(), [](const auto &a, const auto &b) {
        if (a.inMs != b.inMs) return a.inMs < b.inMs;
        return a.id < b.id;
    });
}
}

CaptionTrackModel::CaptionTrackModel(QObject *parent) : QObject(parent)
{
    m_storagePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/caption-track.json");
    m_cues = {
        {QStringLiteral("cue-001"), 1200, 4300, QStringLiteral("A quiet signal crosses the harbor."), m_language, QStringLiteral("default"), 0},
        {QStringLiteral("cue-002"), 5100, 7900, QStringLiteral("We have one more stop."), m_language, QStringLiteral("emphasis"), 0}
    };
    refreshGeometry();
}

void CaptionTrackModel::setSourceId(const QString &value) { if (m_sourceId != value) { m_sourceId = value; emit changed(); } }
void CaptionTrackModel::setTrackId(const QString &value) { if (m_trackId != value) { m_trackId = value; emit changed(); } }
void CaptionTrackModel::setLanguage(const QString &value) { if (m_language != value) { m_language = value; emit changed(); } }
void CaptionTrackModel::setStoragePath(const QString &value) { if (m_storagePath != value) { m_storagePath = value; emit changed(); } }
QString CaptionTrackModel::displayStartMs() const { return QString::number(m_displayStart); }
QString CaptionTrackModel::displayEndMs() const { return QString::number(m_displayEnd); }

int CaptionTrackModel::indexOf(const QVector<CaptionCueRecord> &records, const QString &id) const
{
    for (qsizetype i = 0; i < records.size(); ++i)
        if (records.at(i).id == id) return int(i);
    return -1;
}

QString CaptionTrackModel::selectedCueText() const
{
    const int index = indexOf(m_cues, m_selectedId);
    return index < 0 ? QString() : m_cues.at(index).text;
}

QString CaptionTrackModel::selectedCueInMs() const
{
    const int index = indexOf(m_cues, m_selectedId);
    return index < 0 ? QString() : QString::number(m_cues.at(index).inMs);
}

QString CaptionTrackModel::selectedCueOutMs() const
{
    const int index = indexOf(m_cues, m_selectedId);
    return index < 0 ? QString() : QString::number(m_cues.at(index).outMs);
}

QString CaptionTrackModel::selectedCueStyleId() const
{
    const int index = indexOf(m_cues, m_selectedId);
    return index < 0 ? QString() : m_cues.at(index).styleId;
}

QString CaptionTrackModel::selectedCueLanguage() const
{
    const int index = indexOf(m_cues, m_selectedId);
    return index < 0 ? m_language : m_cues.at(index).language;
}

QVariantList CaptionTrackModel::editorCues() const
{
    const auto &records = m_preview.isEmpty() ? m_cues : m_preview;
    QVariantList result;
    const qreal span = qreal(m_displayEnd - m_displayStart);
    for (const auto &cue : records) {
        const qreal x0 = qBound<qreal>(0, qreal(cue.inMs - m_displayStart) / span, 1);
        const qreal x1 = qBound<qreal>(0, qreal(cue.outMs - m_displayStart) / span, 1);
        if (x1 <= x0) continue;
        result.append(QVariantMap{{QStringLiteral("id"), cue.id},
            {QStringLiteral("inTime"), QString::number(cue.inMs)}, {QStringLiteral("outTime"), QString::number(cue.outMs)},
            {QStringLiteral("x0"), x0}, {QStringLiteral("x1"), x1}, {QStringLiteral("lane"), cue.lane},
            {QStringLiteral("text"), cue.text}, {QStringLiteral("language"), cue.language},
            {QStringLiteral("styleId"), cue.styleId}});
    }
    return result;
}

QVariantList CaptionTrackModel::timeMarks() const
{
    QMap<qint64, QString> marks;
    const qint64 span = m_displayEnd - m_displayStart;
    const qint64 step = qMax<qint64>(1, span / 40);
    for (qint64 time = m_displayStart; time < m_displayEnd; ) {
        const bool labeled = ((time - m_displayStart) / step) % 8 == 0;
        marks.insert(time, labeled ? CaptionTrackCodec::formatTime(time) : QString());
        if (time > m_displayEnd - step) break;
        time += step;
    }
    marks.insert(m_displayEnd, CaptionTrackCodec::formatTime(m_displayEnd));
    for (const auto &cue : m_cues) {
        if (cue.inMs >= m_displayStart && cue.inMs <= m_displayEnd) marks.insert(cue.inMs, QString());
        if (cue.outMs >= m_displayStart && cue.outMs <= m_displayEnd) marks.insert(cue.outMs, QString());
    }
    QVariantList result;
    for (auto it = marks.cbegin(); it != marks.cend(); ++it) {
        const qreal x = qreal(it.key() - m_displayStart) / span;
        result.append(QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("time"), QString::number(it.key())},
                                  {QStringLiteral("label"), it.value()}});
    }
    return result;
}

QVariantList CaptionTrackModel::moveTargets() const
{
    QVariantList result;
    const int selected = indexOf(m_cues, m_selectedId);
    if (selected < 0) return result;
    const auto &cue = m_cues.at(selected);
    const qint64 duration = cue.outMs - cue.inMs;
    const qint64 span = m_displayEnd - m_displayStart;
    const qint64 step = qMax<qint64>(1, span / 40);
    for (qint64 in = m_displayStart; in <= m_displayEnd - duration; ) {
        result.append(QVariantMap{{QStringLiteral("x"), qreal(in - m_displayStart) / span},
            {QStringLiteral("inTime"), QString::number(in)}, {QStringLiteral("outTime"), QString::number(in + duration)}});
        if (in > m_displayEnd - duration - step) break;
        in += step;
    }
    if (result.isEmpty() || cue.inMs < m_displayStart || cue.inMs > m_displayEnd - duration)
        result.append(QVariantMap{{QStringLiteral("x"), qBound<qreal>(0, qreal(cue.inMs - m_displayStart) / span, 1)},
            {QStringLiteral("inTime"), QString::number(cue.inMs)}, {QStringLiteral("outTime"), QString::number(cue.outMs)}});
    return result;
}

QVariantList CaptionTrackModel::styles() const
{
    return {QVariantMap{{QStringLiteral("id"), QStringLiteral("default")}, {QStringLiteral("label"), QStringLiteral("Default")}, {QStringLiteral("supported"), true}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("emphasis")}, {QStringLiteral("label"), QStringLiteral("Emphasis")}, {QStringLiteral("supported"), true}}};
}

void CaptionTrackModel::refreshGeometry() { emit changed(); }

bool CaptionTrackModel::setDisplayRange(const QString &startMilliseconds, const QString &endMilliseconds)
{
    qint64 start = 0, end = 0;
    if (!decimalMilliseconds(startMilliseconds, &start) || !decimalMilliseconds(endMilliseconds, &end) || end <= start) {
        m_error = QStringLiteral("Display range requires non-negative decimal millisecond strings with end after start.");
        emit changed(); return false;
    }
    m_displayStart = start; m_displayEnd = end; m_error.clear(); emit changed(); return true;
}

bool CaptionTrackModel::parseInterval(const QString &inMs, const QString &outMs, qint64 *in, qint64 *out)
{
    if (!decimalMilliseconds(inMs, in) || !decimalMilliseconds(outMs, out) || *out <= *in) {
        m_error = QStringLiteral("Cue times must be exact non-negative decimal millisecond strings with a positive interval.");
        return false;
    }
    return true;
}

bool CaptionTrackModel::validateOverlap(const QVector<CaptionCueRecord> &records, int editedIndex, QString *message) const
{
    const auto &edited = records.at(editedIndex);
    for (qsizetype i = 0; i < records.size(); ++i) {
        if (int(i) == editedIndex || records.at(i).lane != edited.lane) continue;
        const auto &other = records.at(i);
        if (edited.inMs < other.outMs && other.inMs < edited.outMs) {
            *message = QStringLiteral("Cue overlaps %1 in the same display lane.").arg(other.id);
            return false;
        }
    }
    *message = QString(); return true;
}

int CaptionTrackModel::allocateLane(const QVector<CaptionCueRecord> &records, qint64 in, qint64 out) const
{
    for (int lane = 0; lane < 16; ++lane) {
        bool free = true;
        for (const auto &cue : records)
            if (cue.lane == lane && in < cue.outMs && cue.inMs < out) { free = false; break; }
        if (free) return lane;
    }
    return 15;
}

void CaptionTrackModel::setAck(const QString &status, const QString &message)
{
    m_ack = {{QStringLiteral("status"), status}, {QStringLiteral("message"), message}};
}

void CaptionTrackModel::applyImported(QVector<CaptionCueRecord> records, const QString &language)
{
    sortCues(&records);
    QVector<CaptionCueRecord> assigned;
    QSet<QString> ids;
    for (int i = 0; i < records.size(); ++i) {
        auto &cue = records[i];
        if (ids.contains(cue.id)) cue.id += QStringLiteral("-%1").arg(i + 1);
        ids.insert(cue.id);
        if (cue.language.isEmpty() || cue.language == QStringLiteral("und")) cue.language = language;
        cue.lane = allocateLane(assigned, cue.inMs, cue.outMs);
        assigned.append(cue);
    }
    pushUndo(true);
    m_cues = assigned; m_preview.clear(); m_selectedId = m_cues.first().id;
    m_language = language.isEmpty() ? QStringLiteral("und") : language;
    m_error.clear(); setAck(QStringLiteral("accepted"), QStringLiteral("Caption cues loaded.")); emit changed();
}

bool CaptionTrackModel::importWebVtt(const QString &text, const QString &language)
{
    // An explicit language wins; otherwise the file's `Language:` header names
    // the track. Cues in a `<lang>` span keep their own language.
    QVector<CaptionCueRecord> parsed; QString why, header;
    if (!CaptionTrackCodec::parseWebVtt(text, QString(), &parsed, &why, &header)) { m_error = why; setAck(QStringLiteral("rejected"), why); emit changed(); return false; }
    const bool explicitLanguage = !language.isEmpty() && language != QLatin1String("und");
    applyImported(parsed, explicitLanguage ? language : header.isEmpty() ? QStringLiteral("und") : header);
    return true;
}

bool CaptionTrackModel::importSrt(const QString &text, const QString &language)
{
    QVector<CaptionCueRecord> parsed; QString why;
    if (!CaptionTrackCodec::parseSrt(text, language, &parsed, &why)) { m_error = why; setAck(QStringLiteral("rejected"), why); emit changed(); return false; }
    applyImported(parsed, language); return true;
}

QString CaptionTrackModel::exportWebVtt() const { return CaptionTrackCodec::toWebVtt(m_cues, m_language); }
QString CaptionTrackModel::exportSrt() const { return CaptionTrackCodec::toSrt(m_cues); }

QString CaptionTrackModel::toJson() const
{
    return QString::fromUtf8(QJsonDocument(CaptionTrackCodec::toJson(m_cues, m_trackId, m_language)).toJson(QJsonDocument::Indented));
}

bool CaptionTrackModel::loadJson(const QString &json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    QVector<CaptionCueRecord> parsed; QString track, language, why;
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !CaptionTrackCodec::fromJson(document.object(), &parsed, &track, &language, &why)) {
        m_error = why.isEmpty() ? QStringLiteral("Caption track JSON is invalid: %1").arg(parseError.errorString()) : why;
        setAck(QStringLiteral("rejected"), m_error); emit changed(); return false;
    }
    m_cues = parsed; sortCues(&m_cues); m_trackId = track; m_language = language;
    m_selectedId = m_cues.first().id; m_preview.clear(); m_undo.clear(); m_error.clear();
    setAck(QStringLiteral("accepted"), QStringLiteral("Caption track loaded.")); emit changed(); return true;
}

bool CaptionTrackModel::saveToFile(const QString &path)
{
    const QString destination = path.isEmpty() ? m_storagePath : path;
    if (destination.isEmpty()) { m_error = QStringLiteral("Choose a caption track storage path first."); emit changed(); return false; }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        m_error = QStringLiteral("Could not create caption track storage directory."); emit changed(); return false;
    }
    QSaveFile file(destination);
    const QByteArray payload = toJson().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        m_error = QStringLiteral("Could not save caption track to %1.").arg(destination); emit changed(); return false;
    }
    m_storagePath = destination; m_error.clear(); setAck(QStringLiteral("accepted"), QStringLiteral("Caption track saved.")); emit changed(); return true;
}

bool CaptionTrackModel::loadFromFile(const QString &path)
{
    const QString source = path.isEmpty() ? m_storagePath : path;
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) {
        m_error = QStringLiteral("Could not read caption track file within the 8 MiB bound."); emit changed(); return false;
    }
    const bool loaded = loadJson(QString::fromUtf8(file.readAll()));
    if (loaded) { m_storagePath = source; emit changed(); }
    return loaded;
}

bool CaptionTrackModel::addCue(const QString &inMs, const QString &outMs, const QString &text,
                               const QString &language, const QString &styleId)
{
    qint64 in = 0, out = 0;
    if (m_cues.size() >= 100000 || !parseInterval(inMs, outMs, &in, &out)) { emit changed(); return false; }
    QString id;
    do { id = QStringLiteral("cue-%1").arg(m_nextId++, 3, 10, QLatin1Char('0')); }
    while (indexOf(m_cues, id) >= 0);
    CaptionCueRecord cue{id, in, out, text,
                         language.isEmpty() ? m_language : language, styleId, allocateLane(m_cues, in, out)};
    pushUndo();
    m_cues.append(cue); sortCues(&m_cues); m_selectedId = cue.id; m_error.clear();
    setAck(QStringLiteral("accepted"), QStringLiteral("Cue added.")); emit changed(); return true;
}

bool CaptionTrackModel::selectCue(const QString &cueId)
{
    if (indexOf(m_cues, cueId) < 0) return false;
    m_selectedId = cueId; m_preview.clear(); setAck(QString(), QString()); emit changed(); return true;
}

bool CaptionTrackModel::previewCueEdit(const QString &requestId, const QString &cueId, const QString &operation,
                                       const QString &inMs, const QString &outMs, const QString &text, const QString &styleId)
{
    if (operation != QStringLiteral("text") && operation != QStringLiteral("style")
        && operation != QStringLiteral("text-style") && operation != QStringLiteral("move")
        && operation != QStringLiteral("trim-in") && operation != QStringLiteral("trim-out")) {
        m_error = QStringLiteral("Unsupported caption edit operation.");
        setAck(QStringLiteral("rejected"), m_error); emit changed(); return false;
    }
    const int index = indexOf(m_cues, cueId);
    qint64 in = 0, out = 0;
    if (index < 0 || !parseInterval(inMs, outMs, &in, &out)) { setAck(QStringLiteral("rejected"), m_error); emit changed(); return false; }
    QVector<CaptionCueRecord> candidate = m_cues;
    auto &cue = candidate[index]; cue.inMs = in; cue.outMs = out; cue.text = text; cue.styleId = styleId;
    QString why;
    if (!validateOverlap(candidate, index, &why)) {
        m_preview.clear(); m_pendingRequest.clear(); m_pendingCue.clear();
        m_error = why; setAck(QStringLiteral("rejected"), why); emit changed(); return false;
    }
    m_preview = candidate; m_pendingRequest = requestId; m_pendingCue = cueId;
    m_error.clear(); setAck(QStringLiteral("preview"), QStringLiteral("Preview only; commit to save this edit.")); emit changed(); return true;
}

bool CaptionTrackModel::commitCueEdit(const QString &requestId, const QString &cueId, const QString &operation,
                                      const QString &inMs, const QString &outMs, const QString &text, const QString &styleId)
{
    Q_UNUSED(operation)
    const int index = indexOf(m_cues, cueId);
    qint64 in = 0, out = 0;
    if (index < 0 || !parseInterval(inMs, outMs, &in, &out)) { setAck(QStringLiteral("rejected"), m_error); emit changed(); return false; }
    QVector<CaptionCueRecord> candidate = m_cues;
    auto &cue = candidate[index]; cue.inMs = in; cue.outMs = out; cue.text = text; cue.styleId = styleId;
    QString why;
    if (!validateOverlap(candidate, index, &why)) {
        m_preview.clear(); m_pendingRequest.clear(); m_pendingCue.clear();
        m_error = why; setAck(QStringLiteral("rejected"), why); emit changed(); return false;
    }
    pushUndo();
    m_cues = candidate; sortCues(&m_cues); m_preview.clear(); m_pendingRequest.clear(); m_pendingCue.clear();
    m_error.clear(); setAck(QStringLiteral("accepted"), QStringLiteral("Cue edit committed.")); emit changed();
    Q_UNUSED(requestId)
    return true;
}

bool CaptionTrackModel::cancelCueEdit(const QString &requestId, const QString &cueId)
{
    Q_UNUSED(requestId)
    if (cueId != m_pendingCue || m_preview.isEmpty()) return false;
    m_preview.clear(); m_pendingRequest.clear(); m_pendingCue.clear(); m_error.clear();
    setAck(QStringLiteral("canceled"), QStringLiteral("Preview canceled; committed cue is unchanged.")); emit changed(); return true;
}

bool CaptionTrackModel::undo()
{
    if (m_undo.isEmpty()) return false;
    const UndoEntry entry = m_undo.takeLast();
    m_cues = entry.cues; m_preview.clear();
    if (entry.restoresLanguage) m_language = entry.language;
    m_pendingRequest.clear(); m_pendingCue.clear();
    if (indexOf(m_cues, m_selectedId) < 0) m_selectedId = m_cues.isEmpty() ? QString() : m_cues.first().id;
    setAck(QStringLiteral("accepted"), QStringLiteral("Last caption edit undone.")); m_error.clear(); emit changed(); return true;
}
