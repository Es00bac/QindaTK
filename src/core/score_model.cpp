// SPDX-License-Identifier: LGPL-3.0-or-later
// ScoreModel: state, history, persistence, queries and layout access
// (WP-067, docs/notation.md). Structure edits are score_model_structure.cpp,
// entry and notation edits score_model_edit.cpp.
#include "score_model.h"

#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <algorithm>

namespace QindaTK {

ScoreModel::ScoreModel(QObject *parent)
    : QObject(parent), m_content(Score::newScore(QString(), 4, 4, 4, 0))
{
}

bool ScoreModel::commitContent(const ScoreContent &next, const QString &label, QString *error)
{
    if (const QString problem = Score::validate(next); !problem.isEmpty()) {
        if (error) *error = problem;
        return false;
    }
    if (next == m_content) return true;
    m_history.resize(m_cursor);
    m_history.push_back({label, m_content, next});
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = m_history.size();
    m_content = next;
    publish();
    emit historyChanged();
    emit committed(label);
    return true;
}

bool ScoreModel::resetContent(const ScoreContent &next, QString *error)
{
    if (const QString problem = Score::validate(next); !problem.isEmpty()) {
        if (error) *error = problem;
        return false;
    }
    m_content = next;
    m_history.clear();
    m_cursor = 0;
    publish();
    emit historyChanged();
    return true;
}

void ScoreModel::publish()
{
    ++m_revision;
    ++m_layoutRevision;
    emit contentChanged();
    emit layoutChanged();
}

QVariantMap ScoreModel::ok(const QVariantMap &extra)
{
    clearError();
    QVariantMap result = extra;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("revision"), revisionText());
    return result;
}

QVariantMap ScoreModel::failure(const QString &message, const QString &code)
{
    m_lastError = message;
    emit lastErrorChanged();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}, {QStringLiteral("code"), code}};
}

void ScoreModel::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

QVariantMap ScoreModel::editContent(const QString &label, const std::function<QString(ScoreContent &)> &edit,
                                    const QVariantMap &extra)
{
    ScoreContent next = m_content;
    if (const QString problem = edit(next); !problem.isEmpty()) return failure(problem);
    QString error;
    if (!commitContent(next, label, &error)) return failure(error);
    return ok(extra);
}

void ScoreModel::setTitle(const QString &title)
{
    if (title == m_content.title) return;
    editContent(tr("Rename score"), [&title](ScoreContent &c) { c.title = title; return QString(); });
}

void ScoreModel::setComposer(const QString &composer)
{
    if (composer == m_content.composer) return;
    editContent(tr("Set composer"), [&composer](ScoreContent &c) { c.composer = composer; return QString(); });
}

void ScoreModel::layoutOptionChanged()
{
    ++m_layoutRevision;
    m_layoutCache.reset();
    emit layoutChanged();
}

void ScoreModel::setPageWidth(double width)
{
    if (width < 72 || qFuzzyCompare(width, m_options.pageSize.width())) return;
    m_options.pageSize.setWidth(width);
    layoutOptionChanged();
}

void ScoreModel::setPageHeight(double height)
{
    if (height < 72 || qFuzzyCompare(height, m_options.pageSize.height())) return;
    m_options.pageSize.setHeight(height);
    layoutOptionChanged();
}

void ScoreModel::setStaffSpace(double space)
{
    if (space < 2 || space > 40 || qFuzzyCompare(space, m_options.staffSpace)) return;
    m_options.staffSpace = space;
    layoutOptionChanged();
}

void ScoreModel::setMargin(double margin)
{
    if (margin < 0 || qFuzzyCompare(margin + 1, m_options.margin + 1)) return;
    m_options.margin = margin;
    layoutOptionChanged();
}

QVariantMap ScoreModel::capabilities() const
{
    return {{QStringLiteral("durations"), QStringList{QStringLiteral("whole"), QStringLiteral("half"),
                                                      QStringLiteral("quarter"), QStringLiteral("eighth"),
                                                      QStringLiteral("16th"), QStringLiteral("32nd"),
                                                      QStringLiteral("64th")}},
            {QStringLiteral("maxDots"), Score::kMaxDots},
            {QStringLiteral("tuplets"), QStringLiteral("any ratio exact at %1 PPQ, groups within one measure").arg(Score::kPpq)},
            {QStringLiteral("voicesPerStaff"), Score::kMaxVoices},
            {QStringLiteral("stavesPerPart"), Score::kMaxStaves},
            {QStringLiteral("clefs"), QStringList{QStringLiteral("treble"), QStringLiteral("bass"), QStringLiteral("alto")}},
            {QStringLiteral("articulations"), Score::articulationNames()},
            {QStringLiteral("dynamics"), Score::dynamicNames()},
            {QStringLiteral("interchange"), QStringList{QStringLiteral("qindatk.score/1"), QStringLiteral("musicxml-4.0-partwise")}},
            {QStringLiteral("output"), QStringList{QStringLiteral("pdf"), QStringLiteral("image")}},
            {QStringLiteral("pickups"), true},
            // AGENT-CONTRACT: docs/notation.md#not-covered-here lists the same names.
            {QStringLiteral("unsupported"), QStringList{QStringLiteral("grace notes"), QStringLiteral("slurs"),
                                                        QStringLiteral("lyrics"), QStringLiteral("chord symbols"),
                                                        QStringLiteral("cross-staff notes"), QStringLiteral("repeats and endings"),
                                                        QStringLiteral("clef changes"), QStringLiteral("multi-measure rests"),
                                                        QStringLiteral("transposing instruments"),
                                                        QStringLiteral("tempo marks (the session tempo map owns tempo)")}}};
}

QVariantList ScoreModel::parts() const
{
    QVariantList list;
    for (const ScorePart &part : m_content.parts) {
        QVariantList staves;
        for (const ScoreStaff &staff : part.staves) {
            QVariantList voices;
            for (const ScoreVoice &voice : staff.voices)
                voices.push_back(QVariantMap{{QStringLiteral("id"), Score::idText(voice.id)},
                                             {QStringLiteral("number"), voice.number}});
            staves.push_back(QVariantMap{{QStringLiteral("id"), Score::idText(staff.id)},
                                         {QStringLiteral("clef"), Score::clefName(staff.clef)},
                                         {QStringLiteral("voices"), voices}});
        }
        list.push_back(QVariantMap{{QStringLiteral("id"), Score::idText(part.id)},
                                   {QStringLiteral("name"), part.name},
                                   {QStringLiteral("abbreviation"), part.abbreviation},
                                   {QStringLiteral("channel"), part.channel},
                                   {QStringLiteral("staves"), staves}});
    }
    return list;
}

QVariantList ScoreModel::measures() const
{
    const QVector<qint64> starts = Score::measureStarts(m_content);
    QVariantList list;
    for (int m = 0; m < m_content.measures.size(); ++m) {
        const ScoreMeasure &measure = m_content.measures[m];
        list.push_back(QVariantMap{{QStringLiteral("id"), Score::idText(measure.id)},
                                   {QStringLiteral("number"), m + 1},
                                   {QStringLiteral("time"), QStringLiteral("%1/%2").arg(measure.numerator).arg(measure.denominator)},
                                   {QStringLiteral("fifths"), measure.fifths},
                                   {QStringLiteral("break"), Score::breakName(measure.breakAfter)},
                                   {QStringLiteral("pickup"), QString::number(measure.pickup)},
                                   {QStringLiteral("startTicks"), QString::number(starts[m])},
                                   {QStringLiteral("endTicks"), QString::number(starts[m + 1])}});
    }
    return list;
}

const ScoreLayout &ScoreModel::layout(const QVariantMap &options) const
{
    if (m_layoutCache && m_layoutCacheRevision == quint64(m_layoutRevision) && m_layoutCacheOptions == options)
        return *m_layoutCache;
    ScoreLayoutOptions effective = m_options;
    for (const QVariant &id : options.value(QStringLiteral("parts")).toList())
        effective.parts.insert(QUuid::fromString(id.toString()));
    m_layoutCache = Score::layoutScore(m_content, effective);
    m_layoutCacheRevision = quint64(m_layoutRevision);
    m_layoutCacheOptions = options;
    return *m_layoutCache;
}

QVariantMap ScoreModel::layoutSnapshot(int page, const QVariantMap &options) const
{
    return Score::pageSnapshot(layout(options), page, scoreId(), m_revision);
}

QVariantMap ScoreModel::locate(const QString &tickText, const QVariantMap &options) const
{
    bool parsed = false;
    const qint64 tick = tickText.toLongLong(&parsed);
    const ScoreLayout &scoreLayout = layout(options);
    const double pw = std::max(1.0, scoreLayout.pageSize.width()), ph = std::max(1.0, scoreLayout.pageSize.height());
    if (parsed)
        for (const ScorePageLayout &page : scoreLayout.pages)
            for (const ScoreSystemLayout &system : page.systems) {
                if (system.staves.isEmpty()) continue;
                for (const ScoreMeasureLayout &measure : system.staves.front().measures) {
                    if (tick < measure.start || tick >= measure.end) continue;
                    // Between the columns around the tick, in proportion to time.
                    double x0 = measure.contentX, x1 = measure.x1;
                    qint64 t0 = measure.start, t1 = measure.end;
                    for (const auto &[at, x] : measure.columns) {
                        if (at <= tick) { t0 = at; x0 = x; }
                        if (at > tick) { t1 = at; x1 = x; break; }
                    }
                    const double x = x0 + (x1 - x0) * double(tick - t0) / double(std::max<qint64>(1, t1 - t0));
                    return {{QStringLiteral("ok"), true}, {QStringLiteral("page"), page.index},
                            {QStringLiteral("x"), x / pw}, {QStringLiteral("top"), system.top / ph},
                            {QStringLiteral("bottom"), system.bottom / ph},
                            {QStringLiteral("measureId"), Score::idText(measure.id)}};
                }
            }
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Tick %1 is not in the score").arg(tickText)}};
}

QImage ScoreModel::renderPageImage(int page, double zoom, const QVariantMap &options) const
{
    const ScoreLayout &scoreLayout = layout(options);
    const QSize size(qMax(1, qRound(scoreLayout.pageSize.width() * zoom)), qMax(1, qRound(scoreLayout.pageSize.height() * zoom)));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    // 72 dots per inch: one point is one pixel before the zoom, as in the PDF.
    image.setDotsPerMeterX(2835);
    image.setDotsPerMeterY(2835);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(zoom, zoom);
    Score::paintPage(&painter, scoreLayout, page);
    return image;
}

QVariantMap ScoreModel::exportPdf(const QString &file, const QVariantMap &options) const
{
    const ScoreLayout &scoreLayout = layout(options);
    QPdfWriter writer(file);
    writer.setResolution(72); // one unit is one point
    writer.setPageSize(QPageSize(scoreLayout.pageSize, QPageSize::Point, QStringLiteral("Score"), QPageSize::ExactMatch));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(m_content.title);
    QPainter painter;
    if (!painter.begin(&writer))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Cannot write %1").arg(file)}};
    for (int page = 0; page < scoreLayout.pages.size(); ++page) {
        if (page > 0) writer.newPage();
        Score::paintPage(&painter, scoreLayout, page);
    }
    painter.end();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("pages"), int(scoreLayout.pages.size())}};
}

// ---- queries ----

QVariantMap ScoreModel::eventData(const QString &eventId) const
{
    const auto ref = Score::findEvent(m_content, QUuid::fromString(eventId));
    if (!ref.valid()) return {};
    const ScorePart &part = m_content.parts[ref.part];
    const ScoreStaff &staff = part.staves[ref.staff];
    const ScoreVoice &voice = staff.voices[ref.voice];
    const ScoreEvent &event = voice.events[ref.event];
    const int measure = Score::measureIndexAt(Score::measureStarts(m_content), event.tick);
    QVariantMap data = Score::eventMap(event);
    data.insert(QStringLiteral("partId"), Score::idText(part.id));
    data.insert(QStringLiteral("staffId"), Score::idText(staff.id));
    data.insert(QStringLiteral("voiceId"), Score::idText(voice.id));
    data.insert(QStringLiteral("voice"), voice.number);
    data.insert(QStringLiteral("measureId"), Score::idText(m_content.measures.value(measure).id));
    data.insert(QStringLiteral("label"), Score::describe(m_content, event, measure, voice.number));
    return data;
}

QString ScoreModel::adjacentEvent(const QString &eventId, const QString &direction) const
{
    const auto ref = Score::findEvent(m_content, QUuid::fromString(eventId));
    if (!ref.valid()) return {};
    const ScoreStaff &staff = m_content.parts[ref.part].staves[ref.staff];
    const ScoreEvent &event = staff.voices[ref.voice].events[ref.event];
    const auto &events = staff.voices[ref.voice].events;
    if (direction == QLatin1String("previous")) return ref.event > 0 ? Score::idText(events[ref.event - 1].id) : QString();
    if (direction == QLatin1String("next"))
        return ref.event + 1 < events.size() ? Score::idText(events[ref.event + 1].id) : QString();
    // voice-up / voice-down: the event sounding at the same tick in the
    // neighbouring voice (then staff) in reading order.
    QVector<const ScoreVoice *> order;
    int self = -1;
    for (const ScorePart &part : m_content.parts)
        for (const ScoreStaff &s : part.staves)
            for (const ScoreVoice &v : s.voices) {
                if (v.id == staff.voices[ref.voice].id) self = int(order.size());
                order.push_back(&v);
            }
    const int step = direction == QLatin1String("voice-up") ? -1 : direction == QLatin1String("voice-down") ? 1 : 0;
    if (!step) return {};
    for (int i = self + step; i >= 0 && i < order.size(); i += step)
        for (const ScoreEvent &other : order[i]->events)
            if (other.tick <= event.tick && other.tick + Score::ticks(other.duration).value_or(0) > event.tick)
                return Score::idText(other.id);
    return {};
}

QVariantList ScoreModel::readingOrder(const QVariantMap &options) const
{
    QSet<QUuid> parts;
    for (const QVariant &id : options.value(QStringLiteral("parts")).toList()) parts.insert(QUuid::fromString(id.toString()));
    const QVector<qint64> starts = Score::measureStarts(m_content);
    QVariantList list;
    for (int m = 0; m < m_content.measures.size(); ++m)
        for (const ScorePart &part : m_content.parts) {
            if (!parts.isEmpty() && !parts.contains(part.id)) continue;
            for (const ScoreStaff &staff : part.staves)
                for (const ScoreVoice &voice : staff.voices)
                    for (const ScoreEvent &event : voice.events)
                        if (event.tick >= starts[m] && event.tick < starts[m + 1])
                            list.push_back(QVariantMap{{QStringLiteral("eventId"), Score::idText(event.id)},
                                                       {QStringLiteral("label"), Score::describe(m_content, event, m, voice.number)}});
        }
    return list;
}

QString ScoreModel::describeEvent(const QString &eventId) const
{
    return eventData(eventId).value(QStringLiteral("label")).toString();
}

// ---- history and persistence ----

bool ScoreModel::undo()
{
    if (!canUndo()) return false;
    m_content = m_history.at(--m_cursor).before;
    publish();
    emit historyChanged();
    return true;
}

bool ScoreModel::redo()
{
    if (!canRedo()) return false;
    m_content = m_history.at(m_cursor++).after;
    publish();
    emit historyChanged();
    return true;
}

void ScoreModel::clearHistory()
{
    m_history.clear();
    m_cursor = 0;
    emit historyChanged();
}

QString ScoreModel::toJson() const
{
    return QString::fromUtf8(QJsonDocument(Score::toJson(m_content)).toJson(QJsonDocument::Indented));
}

QVariantMap ScoreModel::loadJson(const QString &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (!document.isObject()) return failure(tr("Not a score document: %1").arg(parseError.errorString()));
    QString error;
    const auto content = Score::fromJson(document.object(), &error);
    if (!content || !resetContent(*content, &error)) return failure(error);
    return ok();
}

QVariantMap ScoreModel::contentMap() const
{
    return Score::toJson(m_content).toVariantMap();
}

QVariantMap ScoreModel::loadContent(const QVariantMap &content)
{
    return loadJson(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(content)).toJson(QJsonDocument::Compact)));
}

bool ScoreModel::save(const QString &file)
{
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        failure(tr("Cannot write %1").arg(file));
        return false;
    }
    out.write(toJson().toUtf8());
    if (!out.commit()) {
        failure(tr("Cannot write %1").arg(file));
        return false;
    }
    clearError();
    return true;
}

QVariantMap ScoreModel::open(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return failure(tr("Cannot read %1").arg(file));
    return loadJson(QString::fromUtf8(in.readAll()));
}

} // namespace QindaTK
