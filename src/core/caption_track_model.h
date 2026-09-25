// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "caption_track_codec.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

// AGENT-CONTRACT: CaptionEditor sends exact decimal millisecond strings and
// stable cue IDs to this host model. The model validates per-lane intervals,
// publishes normalized display geometry, and owns format/persistence state.
class CaptionTrackModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString sourceId READ sourceId WRITE setSourceId NOTIFY changed)
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(QString displayStartMs READ displayStartMs NOTIFY changed)
    Q_PROPERTY(QString displayEndMs READ displayEndMs NOTIFY changed)
    Q_PROPERTY(QVariantList editorCues READ editorCues NOTIFY changed)
    Q_PROPERTY(QVariantList timeMarks READ timeMarks NOTIFY changed)
    Q_PROPERTY(QVariantList moveTargets READ moveTargets NOTIFY changed)
    Q_PROPERTY(QVariantList styles READ styles CONSTANT)
    Q_PROPERTY(QString selectedCueId READ selectedCueId NOTIFY changed)
    Q_PROPERTY(QString selectedCueInMs READ selectedCueInMs NOTIFY changed)
    Q_PROPERTY(QString selectedCueOutMs READ selectedCueOutMs NOTIFY changed)
    Q_PROPERTY(QString selectedCueText READ selectedCueText NOTIFY changed)
    Q_PROPERTY(QString selectedCueStyleId READ selectedCueStyleId NOTIFY changed)
    Q_PROPERTY(QString selectedCueLanguage READ selectedCueLanguage NOTIFY changed)
    Q_PROPERTY(QVariantMap acknowledgement READ acknowledgement NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath NOTIFY changed)
    // Reading and overlap warnings (docs/caption-rendering.md "Warnings"):
    // [{cueId, kind, message}], and the selected cue's messages as one line.
    Q_PROPERTY(QVariantList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QString warningText READ warningText NOTIFY changed)
    // Supported formats and what each carries (docs/caption-rendering.md "Formats").
    Q_PROPERTY(QVariantList formats READ formats CONSTANT)

public:
    explicit CaptionTrackModel(QObject *parent = nullptr);
    QString sourceId() const { return m_sourceId; }
    void setSourceId(const QString &value);
    QString trackId() const { return m_trackId; }
    void setTrackId(const QString &value);
    QString language() const { return m_language; }
    void setLanguage(const QString &value);
    QString displayStartMs() const;
    QString displayEndMs() const;
    QVariantList editorCues() const;
    QVariantList timeMarks() const;
    QVariantList moveTargets() const;
    QVariantList styles() const;
    QString selectedCueId() const { return m_selectedId; }
    QString selectedCueInMs() const;
    QString selectedCueOutMs() const;
    QString selectedCueText() const;
    QString selectedCueStyleId() const;
    QString selectedCueLanguage() const;
    QVariantMap acknowledgement() const { return m_ack; }
    QString error() const { return m_error; }
    QString storagePath() const { return m_storagePath; }
    void setStoragePath(const QString &value);
    QVariantList warnings() const;
    QString warningText() const;
    QVariantList formats() const;
    // The committed cues, sorted by in time then ID (C++ consumers such as
    // VideoFinishing read these; previews never leak into output).
    const QVector<CaptionCueRecord> &cues() const { return m_cues; }

    // Reading guidelines behind `warnings`.
    static constexpr qint64 kMinimumDurationMs = 833;
    static constexpr int kMaximumCharactersPerSecond = 20;
    static constexpr int kMaximumLineLength = 42;

    Q_INVOKABLE bool setDisplayRange(const QString &startMilliseconds, const QString &endMilliseconds);
    Q_INVOKABLE bool importWebVtt(const QString &text, const QString &language = QStringLiteral("und"));
    Q_INVOKABLE bool importSrt(const QString &text, const QString &language = QStringLiteral("und"));
    Q_INVOKABLE QString exportWebVtt() const;
    Q_INVOKABLE QString exportSrt() const;
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE bool loadJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path = QString());
    Q_INVOKABLE bool loadFromFile(const QString &path = QString());
    Q_INVOKABLE bool addCue(const QString &inMs, const QString &outMs, const QString &text,
                            const QString &language = QString(), const QString &styleId = QStringLiteral("default"));
    Q_INVOKABLE bool selectCue(const QString &cueId);
    Q_INVOKABLE bool previewCueEdit(const QString &requestId, const QString &cueId, const QString &operation,
                                    const QString &inMs, const QString &outMs,
                                    const QString &text, const QString &styleId);
    Q_INVOKABLE bool commitCueEdit(const QString &requestId, const QString &cueId, const QString &operation,
                                   const QString &inMs, const QString &outMs,
                                   const QString &text, const QString &styleId);
    Q_INVOKABLE bool cancelCueEdit(const QString &requestId, const QString &cueId);
    Q_INVOKABLE bool undo();

    // caption_track_edits.cpp. Each is one undo step; a rejection changes
    // nothing and explains itself in `error` and `acknowledgement`.
    // Splits a cue at an exact time strictly inside it; the text splits at
    // `textPosition` (-1: the whitespace nearest the time's proportion; a
    // text without whitespace is kept whole on both parts).
    Q_INVOKABLE bool splitCue(const QString &cueId, const QString &atMs, int textPosition = -1);
    // Joins two cues of one language into the first ID: the union interval,
    // texts joined by a line break in time order.
    Q_INVOKABLE bool mergeCues(const QString &firstCueId, const QString &secondCueId);
    // Moves the listed cues (all cues when empty) by a signed decimal
    // millisecond delta; refused when a cue would start before zero or
    // overlap an unmoved cue in its lane.
    Q_INVOKABLE bool shiftCues(const QStringList &cueIds, const QString &deltaMs);
    // Replaces the track with the first text subtitle stream of a media file
    // (MediaCaptions::readEmbedded); the stream's language becomes the track's.
    Q_INVOKABLE bool importEmbedded(const QString &mediaPath);

signals:
    void changed();

private:
    int indexOf(const QVector<CaptionCueRecord> &records, const QString &id) const;
    bool parseInterval(const QString &inMs, const QString &outMs, qint64 *in, qint64 *out);
    bool validateOverlap(const QVector<CaptionCueRecord> &records, int editedIndex, QString *message) const;
    int allocateLane(const QVector<CaptionCueRecord> &records, qint64 in, qint64 out) const;
    void setAck(const QString &status, const QString &message);
    void applyImported(QVector<CaptionCueRecord> records, const QString &language);
    void refreshGeometry();
    // `withLanguage`: the step also changes the track language (an import),
    // so undo restores that language with the cues.
    void pushUndo(bool withLanguage = false);
    QString allocateId();
    bool rejectEdit(const QString &message);
    void acceptEdit(QVector<CaptionCueRecord> cues, const QString &selected, const QString &message);

    QString m_sourceId = QStringLiteral("source-video");
    QString m_trackId = QStringLiteral("captions-main");
    QString m_language = QStringLiteral("en-US");
    QString m_selectedId;
    QString m_storagePath;
    QString m_error;
    QVariantMap m_ack;
    qint64 m_displayStart = 0;
    qint64 m_displayEnd = 30000;
    int m_nextId = 1;
    QVector<CaptionCueRecord> m_cues;
    QVector<CaptionCueRecord> m_preview;
    // AGENT-GUARD: an import replaces the track language as well as the cues;
    // its undo entry restores both, or the old cues would be exported under
    // the imported language (WebVTT header, <lang> spans, embedded tag).
    struct UndoEntry {
        QVector<CaptionCueRecord> cues;
        QString language;
        bool restoresLanguage = false;
    };
    QVector<UndoEntry> m_undo;
    QString m_pendingRequest;
    QString m_pendingCue;
};
