// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "score_layout.h"
#include "score_types.h"

#include <QImage>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (D-178, docs/notation.md): the persistent score behind
// ScoreView. ScoreView requests map one-to-one onto the invokables below
// (noteEntryRequested -> insertNote, restEntryRequested -> insertRest,
// notationEditRequested -> editEvent, eventDeleteRequested -> deleteEvents,
// navigateRequested -> adjacentEvent) and `layoutSnapshot(page)` is the
// view's `layoutSnapshot`. Every accepted change is validated whole
// (Score::validate) and recorded as one step of the model's own history;
// undo restores the complete previous content. A refused change changes
// nothing and records nothing. Results are {ok, error, ...}.
class ScoreModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString schemaId READ schemaId CONSTANT)
    Q_PROPERTY(QString scoreId READ scoreId NOTIFY contentChanged)
    Q_PROPERTY(QString revision READ revisionText NOTIFY contentChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY contentChanged)
    Q_PROPERTY(QString composer READ composer WRITE setComposer NOTIFY contentChanged)
    Q_PROPERTY(QVariantList parts READ parts NOTIFY contentChanged)
    Q_PROPERTY(QVariantList measures READ measures NOTIFY contentChanged)
    Q_PROPERTY(QString totalTicks READ totalTicksText NOTIFY contentChanged)
    Q_PROPERTY(double pageWidth READ pageWidth WRITE setPageWidth NOTIFY layoutChanged)
    Q_PROPERTY(double pageHeight READ pageHeight WRITE setPageHeight NOTIFY layoutChanged)
    Q_PROPERTY(double staffSpace READ staffSpace WRITE setStaffSpace NOTIFY layoutChanged)
    Q_PROPERTY(double margin READ margin WRITE setMargin NOTIFY layoutChanged)
    Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY layoutChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kHistoryLimit = 256;

    explicit ScoreModel(QObject *parent = nullptr);
    static QString schemaId() { return QStringLiteral("qindatk.score/1"); }

    // ---- C++ state ----
    const ScoreContent &content() const { return m_content; }
    bool commitContent(const ScoreContent &next, const QString &label, QString *error = nullptr);
    // Replaces the content without history (open, import, new score).
    bool resetContent(const ScoreContent &next, QString *error = nullptr);
    quint64 revisionValue() const { return m_revision; }
    QString revisionText() const { return QString::number(m_revision); }
    QString scoreId() const { return Score::idText(m_content.id); }
    QString title() const { return m_content.title; }
    void setTitle(const QString &title);
    QString composer() const { return m_content.composer; }
    void setComposer(const QString &composer);
    QVariantList parts() const;
    QVariantList measures() const;
    QString totalTicksText() const { return QString::number(Score::totalTicks(m_content)); }
    double pageWidth() const { return m_options.pageSize.width(); }
    void setPageWidth(double width);
    double pageHeight() const { return m_options.pageSize.height(); }
    void setPageHeight(double height);
    double staffSpace() const { return m_options.staffSpace; }
    void setStaffSpace(double space);
    double margin() const { return m_options.margin; }
    void setMargin(double margin);
    int layoutRevision() const { return m_layoutRevision; }
    int pageCount() const { return int(layout().pages.size()); }
    QVariantMap capabilities() const;
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const { return canUndo() ? m_history.at(m_cursor - 1).label : QString(); }
    QString redoText() const { return canRedo() ? m_history.at(m_cursor).label : QString(); }
    QString lastError() const { return m_lastError; }
    // The layout for `options` ({parts: [part IDs]}; empty: every part), cached per revision.
    const ScoreLayout &layout(const QVariantMap &options = {}) const;
    QImage renderPageImage(int page, double zoom, const QVariantMap &options = {}) const;

    // ---- structure (score_model_structure.cpp) ----
    Q_INVOKABLE QVariantMap newScore(const QString &title, int measures = 4, const QString &time = QStringLiteral("4/4"),
                                     int fifths = 0);
    // clefs: "treble" | "bass" | "alto", one per staff (1..4)
    Q_INVOKABLE QVariantMap addPart(const QString &name, const QStringList &clefs = {QStringLiteral("treble")},
                                    int channel = -1);
    Q_INVOKABLE QVariantMap removePart(const QString &partId);
    // fields: name, abbreviation, channel (0..15)
    Q_INVOKABLE QVariantMap setPart(const QString &partId, const QVariantMap &fields);
    Q_INVOKABLE QVariantMap setClef(const QString &staffId, const QString &clef);
    Q_INVOKABLE QVariantMap addVoice(const QString &staffId);
    Q_INVOKABLE QVariantMap appendMeasures(int count);
    Q_INVOKABLE QVariantMap insertMeasures(const QString &beforeMeasureId, int count);
    Q_INVOKABLE QVariantMap removeMeasures(const QStringList &measureIds);
    // Applies to the measure and the following ones that had the same value.
    Q_INVOKABLE QVariantMap setTimeSignature(const QString &measureId, int numerator, int denominator);
    Q_INVOKABLE QVariantMap setKeySignature(const QString &measureId, int fifths);
    // A pickup (anacrusis) of `ticks` shorter than the meter; "0" restores the full bar.
    Q_INVOKABLE QVariantMap setPickup(const QString &measureId, const QString &ticks);
    Q_INVOKABLE QVariantMap setBreak(const QString &measureId, const QString &kind); // none | system | page

    // ---- entry and notation edits (score_model_edit.cpp) ----
    Q_INVOKABLE QVariantMap insertNote(const QString &partId, const QString &staffId, const QString &voiceId,
                                       const QString &measureId, const QString &positionTicks,
                                       const QString &pitch, const QString &durationId);
    Q_INVOKABLE QVariantMap insertRest(const QString &partId, const QString &staffId, const QString &voiceId,
                                       const QString &measureId, const QString &positionTicks,
                                       const QString &durationId);
    // edit: duration (value: duration ID), dot, tie (value: note ID or ""),
    // articulation (name), dynamic (name or ""), pitch (pitch name; map
    // {noteId, pitch}), step (+1/-1 diatonic), transpose (semitones),
    // respell (note ID or ""), addPitch (pitch name), removePitch (note ID), rest
    Q_INVOKABLE QVariantMap editEvent(const QString &eventId, const QString &edit, const QVariant &value = {});
    // A note becomes a rest of the same value; a rest in voice 2-4 is removed.
    Q_INVOKABLE QVariantMap deleteEvents(const QStringList &eventIds);

    // ---- queries (score_model.cpp) ----
    Q_INVOKABLE QVariantMap eventData(const QString &eventId) const;
    // direction: previous | next | voice-up | voice-down
    Q_INVOKABLE QString adjacentEvent(const QString &eventId, const QString &direction) const;
    Q_INVOKABLE QVariantList rhythmReport() const { return Score::rhythmReport(m_content); }
    // Every event in reading order (measure, part, staff, voice, time) with its spoken description.
    Q_INVOKABLE QVariantList readingOrder(const QVariantMap &options = {}) const;
    Q_INVOKABLE QString describeEvent(const QString &eventId) const;
    Q_INVOKABLE QVariantMap layoutSnapshot(int page, const QVariantMap &options = {}) const;
    // Where score tick `tick` is drawn: {page, x, top, bottom, measureId} (normalized).
    Q_INVOKABLE QVariantMap locate(const QString &tick, const QVariantMap &options = {}) const;

    // ---- history and persistence (score_model.cpp) ----
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE QVariantMap loadJson(const QString &json);
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    Q_INVOKABLE bool save(const QString &file);
    Q_INVOKABLE QVariantMap open(const QString &file);

    // ---- output and interchange ----
    // PDF pages from the same layout the view draws (score_render.cpp).
    Q_INVOKABLE QVariantMap exportPdf(const QString &file, const QVariantMap &options = {}) const;
    // MusicXML 4.0 partwise (score_musicxml_write.cpp / _read.cpp); import
    // replaces the content as one undo step and reports what it could not keep.
    Q_INVOKABLE QVariantMap exportMusicXml(const QString &file, const QVariantMap &options = {}) const;
    Q_INVOKABLE QVariantMap importMusicXml(const QString &file);
    // The MIDI rendering at `ticksPerQuarter` (score_midi.cpp), for inspection.
    Q_INVOKABLE QVariantMap midiConversion(int ticksPerQuarter = Score::kPpq) const;

signals:
    void contentChanged();
    void layoutChanged();
    void historyChanged();
    void lastErrorChanged();
    void committed(const QString &label);

private:
    struct Step { QString label; ScoreContent before, after; };
    QVariantMap editContent(const QString &label, const std::function<QString(ScoreContent &)> &edit,
                            const QVariantMap &extra = {});
    void publish();
    void layoutOptionChanged();
    QVariantMap ok(const QVariantMap &extra = {});
    QVariantMap failure(const QString &message, const QString &code = QStringLiteral("refused"));
    void clearError();

    ScoreContent m_content;
    QVector<Step> m_history;
    qsizetype m_cursor = 0;
    quint64 m_revision = 0;
    int m_layoutRevision = 0;
    ScoreLayoutOptions m_options;
    mutable std::optional<ScoreLayout> m_layoutCache;
    mutable QVariantMap m_layoutCacheOptions;
    mutable quint64 m_layoutCacheRevision = 0;
    QString m_lastError;
};

} // namespace QindaTK
