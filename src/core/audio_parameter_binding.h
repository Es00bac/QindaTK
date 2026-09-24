// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_parameter_map.h"
#include "audio_session.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <limits>

namespace QindaTK {

// AGENT-CONTRACT (WP-061, D-100, docs/audio-measurement.md): binds Knob and
// Fader gestures to ONE engine parameter: an AutomationModel parameter of an
// AudioSession, which the engine reads by ID through EngineScheduleSnapshot.
// begin -> preview* -> commit | cancel is one transaction:
//   - while held, the engine plays the gesture value (in every mode);
//   - commit records at most one history step: the automation pass when the
//     mode writes during playback ("recorded"), otherwise a static-value edit
//     (the parameter's defaultValue) when the lane has no points or its mode
//     is off ("static"); in read mode over a curve nothing is stored ("live");
//     an unchanged value stores nothing ("unchanged");
//   - cancel stores nothing: a pass opened by the gesture is discarded.
// `value`/`silent` are what the engine plays at the transport position. A dB
// gain's range minimum is silence (D-101). The control never owns the value.
class AudioParameterBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::AudioSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString parameterId READ parameterId WRITE setParameterId NOTIFY parameterIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY descriptorChanged)
    Q_PROPERTY(QString name READ name NOTIFY descriptorChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY descriptorChanged)
    Q_PROPERTY(QString taper READ taper NOTIFY descriptorChanged)
    Q_PROPERTY(double minimum READ minimum NOTIFY descriptorChanged)
    Q_PROPERTY(double maximum READ maximum NOTIFY descriptorChanged)
    Q_PROPERTY(double step READ step NOTIFY descriptorChanged)
    Q_PROPERTY(bool silenceFloor READ silenceFloor NOTIFY descriptorChanged)
    Q_PROPERTY(double staticValue READ staticValue NOTIFY descriptorChanged)
    Q_PROPERTY(double resetValue READ resetValue WRITE setResetValue NOTIFY descriptorChanged)
    Q_PROPERTY(double unityPosition READ unityPosition WRITE setUnityPosition NOTIFY descriptorChanged)
    Q_PROPERTY(QindaTK::AudioParameterMap *mapper READ mapper CONSTANT)
    Q_PROPERTY(double value READ value NOTIFY valueChanged)
    Q_PROPERTY(bool silent READ silent NOTIFY valueChanged)
    Q_PROPERTY(bool automated READ automated NOTIFY valueChanged)
    Q_PROPERTY(QString valueText READ valueText NOTIFY valueChanged)
    Q_PROPERTY(QString automationMode READ automationMode NOTIFY automationChanged)
    Q_PROPERTY(QString automationState READ automationState NOTIFY automationChanged)
    Q_PROPERTY(bool touched READ touched NOTIFY automationChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY automationChanged)
    Q_PROPERTY(bool gestureActive READ gestureActive NOTIFY gestureChanged)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY gestureChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit AudioParameterBinding(QObject *parent = nullptr);
    ~AudioParameterBinding() override;

    AudioSession *session() const { return m_session; }
    void setSession(AudioSession *session);
    QString parameterId() const { return m_parameterId; }
    void setParameterId(const QString &parameterId);
    bool valid() const { return m_valid; }
    QString name() const { return m_parameter.name; }
    QString unit() const;
    QString taper() const;
    double minimum() const { return m_parameter.range.minimum; }
    double maximum() const { return m_parameter.range.maximum; }
    double step() const { return m_parameter.step; }
    bool silenceFloor() const;
    double staticValue() const { return m_parameter.defaultValue; }
    // NaN (the default) resets to 0 clamped into the range: unity for a gain.
    double resetValue() const;
    void setResetValue(double value);
    double unityPosition() const { return m_unityPosition; }
    void setUnityPosition(double position);
    AudioParameterMap *mapper() const { return m_mapper; }
    double value() const { return m_value; }
    bool silent() const { return m_silent; }
    bool automated() const { return m_automated; }
    QString valueText() const { return format(m_value, m_silent); }
    QString automationMode() const { return m_mode; }
    QString automationState() const;
    bool touched() const { return m_touched; }
    bool writing() const { return m_writing; }
    bool gestureActive() const { return m_active; }
    QString lastResult() const { return m_lastResult; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool begin(double value, bool silent = false);
    Q_INVOKABLE bool preview(double value, bool silent = false);
    Q_INVOKABLE bool commit(double value, bool silent = false);
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE bool setAutomationMode(const QString &mode);
    // The value the engine stores for (value, silent): silence is the range
    // minimum of a dB gain; anything else is clamped and put on the step grid.
    Q_INVOKABLE double canonical(double value, bool silent = false) const;
    Q_INVOKABLE QString format(double value, bool silent = false) const;

signals:
    void sessionChanged();
    void parameterIdChanged();
    void descriptorChanged();
    void valueChanged();
    void automationChanged();
    void gestureChanged();
    void errorChanged();
    // result: "static", "recorded", "live", "unchanged", "canceled" or "rejected".
    void transactionFinished(const QString &result, double value, bool silent);

private:
    AutomationModel *automation() const;
    const AutomationModel::Lane *lane() const;
    qint64 ticks() const;
    void reconnect();
    void refresh();
    void refreshDescriptor();
    void refreshValue();
    void refreshAutomation();
    void updateMapper();
    void release();
    void finish(const QString &result);
    bool fail(const QString &message);
    void clearError();

    QPointer<AudioSession> m_session;
    QString m_parameterId;
    QList<QMetaObject::Connection> m_connections;
    AudioParameterMap *m_mapper = nullptr;
    AutomationParameter m_parameter;
    bool m_valid = false;
    double m_resetValue = std::numeric_limits<double>::quiet_NaN();
    double m_unityPosition = 0.75;
    double m_value = 0.0;
    bool m_silent = false;
    bool m_automated = false;
    QString m_mode;
    bool m_touched = false;
    bool m_writing = false;
    bool m_latched = false;
    bool m_active = false;
    bool m_wasWriting = false;
    double m_startValue = 0.0;
    QString m_lastResult;
    QString m_lastError;
};

} // namespace QindaTK
