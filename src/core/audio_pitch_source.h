// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_analysis_source.h"
#include "audio_pitch.h"

#include <QPointer>
#include <memory>

namespace QindaTK {

// AGENT-CONTRACT (D-194…D-197, docs/audio-pitch.md): the pitch host behind
// PitchEditor. It analyses one resource of an AudioSession (through
// session.edits, so it measures what plays) on the thread pool, once per
// committed buffer, and publishes `snapshot` in PitchEditor's
// analysisSnapshot format for the view range. Edits go through
// session.edits as the "pitch" operation: previewEdit/commitEdit/cancelEdit
// take PitchEditor's request arguments unchanged.
//
// AGENT-GUARD: a ready preview makes session.edits return the previewed
// samples. The source then *holds* its analysis of the committed samples
// (`holding`), so notes keep their IDs through a drag and stale previewed
// pitch never becomes the thing the next request edits. Any change to the
// committed samples marks the analysis stale (`current` false) and
// editFor() refuses until the new analysis is published; a result computed
// for an older revision is never published (stillCurrent()).
class AudioPitchSource : public AudioAnalysisSource {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double minimumHz READ minimumHz WRITE setMinimumHz NOTIFY settingsChanged)
    Q_PROPERTY(double maximumHz READ maximumHz WRITE setMaximumHz NOTIFY settingsChanged)
    Q_PROPERTY(double voicingThreshold READ voicingThreshold WRITE setVoicingThreshold NOTIFY settingsChanged)
    Q_PROPERTY(double correctionAmount READ correctionAmount WRITE setCorrectionAmount NOTIFY settingsChanged)
    Q_PROPERTY(int pitchSnapCents READ pitchSnapCents WRITE setPitchSnapCents NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY resultChanged)
    Q_PROPERTY(bool current READ isCurrent NOTIFY resultChanged)
    Q_PROPERTY(bool holding READ holding NOTIFY resultChanged)
    Q_PROPERTY(int noteCount READ noteCount NOTIFY resultChanged)
    Q_PROPERTY(int analysisRuns READ analysisRuns NOTIFY resultChanged)
    Q_PROPERTY(QString detector READ detector CONSTANT)
    Q_PROPERTY(QString corrector READ corrector CONSTANT)
public:
    explicit AudioPitchSource(QObject *parent = nullptr);

    double minimumHz() const { return m_settings.minimumHz; }
    void setMinimumHz(double hz);
    double maximumHz() const { return m_settings.maximumHz; }
    void setMaximumHz(double hz);
    double voicingThreshold() const { return m_settings.threshold; }
    void setVoicingThreshold(double threshold);
    double correctionAmount() const { return m_amount; }
    void setCorrectionAmount(double amount);
    int pitchSnapCents() const { return m_snapCents; }
    void setPitchSnapCents(int cents);
    QVariantMap snapshot() const { return m_snapshot; }
    bool isCurrent() const { return m_current; }
    bool holding() const { return m_holding; }
    int noteCount() const { return m_analysis ? int(m_analysis->notes.size()) : 0; }
    int analysisRuns() const { return m_runs; }
    QString detector() const { return QStringLiteral("YIN"); }
    QString corrector() const { return QStringLiteral("TD-PSOLA"); }

    // {ok, sample, hz, pitchCents, confidence, voiced} of the frame holding `sample`.
    Q_INVOKABLE QVariantMap frameAt(const QString &sample) const;
    // {ok, id, segmentId, startSample, endSample, pitchCents, label} of the note holding `sample`.
    Q_INVOKABLE QVariantMap noteAt(const QString &sample) const;
    // Every note of the analysed source (not only the view range).
    Q_INVOKABLE QVariantList notes() const;
    // The session.edits map for one PitchEditor request: {ok, error, unchanged, edit}.
    // Targets are clamped to the declared limits; the pitch target snaps to
    // pitchSnapCents when that is positive.
    Q_INVOKABLE QVariantMap editFor(const QString &segmentId, const QString &noteId, const QString &operation,
                                    const QString &anchorSample, const QString &targetSample,
                                    double targetPitchCents, double targetFormantRatio) const;
    // PitchEditor's editPreviewRequested / editCommitRequested / gestureCanceled.
    Q_INVOKABLE QVariantMap previewEdit(const QString &gestureId, const QString &segmentId, const QString &noteId,
                                        const QString &operation, const QString &anchorSample,
                                        const QString &targetSample, double targetPitchCents,
                                        double targetFormantRatio);
    Q_INVOKABLE QVariantMap commitEdit(const QString &gestureId, const QString &segmentId, const QString &noteId,
                                       const QString &operation, const QString &anchorSample,
                                       const QString &targetSample, double targetPitchCents,
                                       double targetFormantRatio);
    Q_INVOKABLE bool cancelEdit(const QString &gestureId);
    // Writes a mono float WAV of harmonic voice-like tones (1/h harmonics under
    // a 1 kHz formant, 1 ms fades): [{pitchCents, seconds, vibratoCents}],
    // pitchCents absent or NaN for silence. {ok, error, path, frames}.
    Q_INVOKABLE QVariantMap writeTestSignal(const QString &file, const QVariantList &notes, int sampleRate = 48000);

signals:
    void settingsChanged();

protected:
    void run() override;
    void invalidate() override;
    void settingsTouched() override;

private:
    struct Located {
        const AudioPitch::Note *note = nullptr;
        const AudioPitch::Segment *segment = nullptr;
        int index = -1;
    };
    void analysisSettingsChanged();
    void publish();
    void clearAnalysis();
    Located locate(const QString &segmentId, const QString &noteId) const;
    QString noteId(const AudioPitch::Note &note) const;
    QString segmentId(const AudioPitch::Segment &segment) const;

    AudioPitch::Settings m_settings;
    double m_amount = 0.0;
    int m_snapCents = 0;
    std::shared_ptr<const AudioPitch::Analysis> m_analysis;
    AudioBufferPtr m_analysisBuffer;   // the committed samples m_analysis measured
    int m_analysisChannel = -1;
    QPointer<AudioSession> m_analysisSession;
    QString m_analysisResource;
    quint64 m_pendingSerial = 0;       // request serial of the analysis in flight; 0 when none
    int m_pendingChannel = -1;
    bool m_current = false;
    bool m_holding = false;
    int m_runs = 0;
    QVariantMap m_snapshot;
    QString m_previewGesture;
    QVariantMap m_previewEdit;
};

} // namespace QindaTK
