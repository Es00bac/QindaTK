// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <mutex>
#include <QtQml/qqmlregistration.h>

class QJSEngine;

namespace QindaTK {
class CommandRegistry;
class UndoService;

// Scripts inspect JSON copies and enqueue semantic CommandRegistry actions.
// No application QObject, delegate, document pointer, or engine is injected.
class ScriptRuntime : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CommandRegistry *commandRegistry READ commandRegistry WRITE setCommandRegistry NOTIFY configurationChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService WRITE setUndoService NOTIFY configurationChanged)
    Q_PROPERTY(QStringList exposedCommandIds READ exposedCommandIds WRITE setExposedCommandIds NOTIFY configurationChanged)
    Q_PROPERTY(QVariantMap documentSnapshot READ documentSnapshot WRITE setDocumentSnapshot NOTIFY configurationChanged)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QVariant lastResult READ lastResult NOTIFY changed)
    Q_PROPERTY(QVariantList recordedCommands READ recordedCommands NOTIFY changed)
    Q_PROPERTY(int maxRuntimeMs READ maxRuntimeMs WRITE setMaxRuntimeMs NOTIFY configurationChanged)
public:
    explicit ScriptRuntime(QObject *parent = nullptr);
    ~ScriptRuntime() override;

    CommandRegistry *commandRegistry() const { return m_registry; }
    void setCommandRegistry(CommandRegistry *registry);
    UndoService *undoService() const { return m_undo; }
    void setUndoService(UndoService *undo);
    QStringList exposedCommandIds() const { return m_exposedCommandIds; }
    void setExposedCommandIds(const QStringList &ids);
    QVariantMap documentSnapshot() const { return m_documentSnapshot; }
    void setDocumentSnapshot(const QVariantMap &snapshot);
    bool running() const { return m_running; }
    QString status() const { return m_status; }
    QString output() const { return m_output; }
    QString errorString() const { return m_error; }
    QVariant lastResult() const { return m_result; }
    QVariantList recordedCommands() const { return m_recordedCommands; }
    int maxRuntimeMs() const { return m_maxRuntimeMs; }
    void setMaxRuntimeMs(int milliseconds);

    Q_INVOKABLE bool execute(const QString &source);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool replay(const QVariantList &commands);
    Q_INVOKABLE bool replayRecorded();
    Q_INVOKABLE QString exportRecordedMacro() const;
    Q_INVOKABLE bool importRecordedMacro(const QString &json);
    Q_INVOKABLE void clear();
signals:
    void configurationChanged();
    void changed();
    void finished(bool success);
private:
    struct ExecutionState {
        std::atomic_bool cancelled{false};
        std::mutex mutex;
        QJSEngine *engine = nullptr; // guarded; setInterrupted is thread-safe
    };
    struct Evaluation {
        QString error;
        QString output;
        QVariant result;
        QVariantList commands;
    };
    static Evaluation evaluate(const QString &source, const QVariantMap &snapshot,
                               const QVariantList &commandInfo,
                               const std::shared_ptr<ExecutionState> &state);
    bool runCommands(const QVariantList &commands);
    void finishEvaluation(quint64 generation, Evaluation evaluation);
    void cancelWithReason(const QString &reason);
    void finishFailure(const QString &message);

    QPointer<CommandRegistry> m_registry;
    QPointer<UndoService> m_undo;
    QStringList m_exposedCommandIds;
    QVariantMap m_documentSnapshot;
    bool m_running = false;
    QString m_status = QStringLiteral("idle");
    QString m_output;
    QString m_error;
    QVariant m_result;
    QVariantList m_recordedCommands;
    int m_maxRuntimeMs = 2000;
    quint64 m_generation = 0;
    QString m_cancelReason;
    QPointer<QThread> m_worker;
    std::shared_ptr<ExecutionState> m_execution;
};
} // namespace QindaTK
