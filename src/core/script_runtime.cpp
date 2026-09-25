// SPDX-License-Identifier: LGPL-3.0-or-later
#include "script_runtime.h"
#include "command_registry.h"
#include "undo_service.h"

#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

namespace QindaTK {
ScriptRuntime::ScriptRuntime(QObject *parent) : QObject(parent) {}

ScriptRuntime::~ScriptRuntime()
{
    cancel();
    if (m_worker) m_worker->wait();
}

void ScriptRuntime::setCommandRegistry(CommandRegistry *registry)
{
    if (m_registry == registry) return;
    m_registry = registry;
    emit configurationChanged();
}

void ScriptRuntime::setUndoService(UndoService *undo)
{
    if (m_undo == undo) return;
    m_undo = undo;
    emit configurationChanged();
}

void ScriptRuntime::setExposedCommandIds(const QStringList &ids)
{
    QStringList filtered;
    for (const QString &id : ids) {
        if (!id.isEmpty() && !filtered.contains(id)) filtered.append(id);
    }
    if (m_exposedCommandIds == filtered) return;
    m_exposedCommandIds = filtered;
    emit configurationChanged();
}

void ScriptRuntime::setDocumentSnapshot(const QVariantMap &snapshot)
{
    if (m_documentSnapshot == snapshot) return;
    // The JSON round trip rejects QObject/pointer references at this boundary.
    m_documentSnapshot = QJsonDocument::fromVariant(snapshot).object().toVariantMap();
    emit configurationChanged();
}

void ScriptRuntime::setMaxRuntimeMs(int milliseconds)
{
    const int bounded = qBound(100, milliseconds, 10000);
    if (m_maxRuntimeMs == bounded) return;
    m_maxRuntimeMs = bounded;
    emit configurationChanged();
}

bool ScriptRuntime::execute(const QString &source)
{
    if (m_running) return false;
    if (source.toUtf8().size() > 65536) {
        finishFailure(QStringLiteral("Script exceeds the 64 KiB source limit"));
        return false;
    }
    m_running = true;
    m_status = QStringLiteral("running");
    m_output.clear();
    m_error.clear();
    m_result.clear();
    m_cancelReason.clear();
    m_recordedCommands.clear();
    emit changed();

    // AGENT-CONTRACT: worker receives only values. Registry handlers and undo
    // always run on this object's owner thread after evaluation has stopped.
    QVariantList commandInfo;
    if (m_registry) {
        for (const QString &id : std::as_const(m_exposedCommandIds)) {
            const QVariantMap row = m_registry->query(id);
            if (row.value(QStringLiteral("enabled")).toBool()
                && row.value(QStringLiteral("visible")).toBool()) commandInfo.append(row);
        }
    }
    const auto snapshot = m_documentSnapshot;
    const auto state = std::make_shared<ExecutionState>();
    m_execution = state;
    const quint64 generation = ++m_generation;
    auto *worker = QThread::create([this, source, snapshot, commandInfo, state, generation] {
        Evaluation result = evaluate(source, snapshot, commandInfo, state);
        QMetaObject::invokeMethod(this, [this, generation, result = std::move(result)]() mutable {
            finishEvaluation(generation, std::move(result));
        }, Qt::QueuedConnection);
    });
    m_worker = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
    QTimer::singleShot(m_maxRuntimeMs, this, [this, generation] {
        if (m_running && generation == m_generation)
            cancelWithReason(QStringLiteral("Script time limit exceeded"));
    });
    return true;
}

ScriptRuntime::Evaluation ScriptRuntime::evaluate(
    const QString &source, const QVariantMap &snapshot, const QVariantList &commandInfo,
    const std::shared_ptr<ExecutionState> &state)
{
    Evaluation evaluation;
    QJSEngine engine;
    {
        const std::lock_guard lock(state->mutex);
        state->engine = &engine;
        if (state->cancelled.load()) engine.setInterrupted(true);
    }
    // AGENT-NOTE: values enter as JSON text parsed by the engine. toScriptValue
    // turns a QVariantList nested in a map into a sequence wrapper that is not
    // Array.isArray, so freezeDeep copied it into a plain object and scripts
    // lost map/filter/length on nested lists (tst_notebook_services,
    // codeBlocksEvaluateThroughTheScriptRuntime). Both inputs are JSON already.
    const QJSValue parse = engine.globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("parse"));
    engine.globalObject().setProperty(QStringLiteral("__document"), parse.call({QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(snapshot)).toJson(QJsonDocument::Compact))}));
    engine.globalObject().setProperty(QStringLiteral("__commands"), parse.call({QString::fromUtf8(
        QJsonDocument(QJsonArray::fromVariantList(commandInfo)).toJson(QJsonDocument::Compact))}));
    // All JS-visible application values above are copies, never newQObject().
    // QJSEngine is a local automation facility, not a hostile-code sandbox.
    const QString prefix = QStringLiteral(R"JS((function() {
        "use strict";
        const actions = [], lines = [];
        // Freeze fresh copies: dense arrays and plain objects. Qt 6.8's
        // Object.freeze crashes on the sparse arrays that converted host
        // values can use (QV4::SparseArrayData).
        function freezeDeep(value) {
            if (Array.isArray(value)) return Object.freeze(Array.from(value, freezeDeep));
            if (value && typeof value === 'object') {
                const copy = {};
                Object.keys(value).forEach(key => { copy[key] = freezeDeep(value[key]); });
                return Object.freeze(copy);
            }
            return value;
        }
        const document = freezeDeep(__document);
        const commands = freezeDeep(__commands);
        const app = Object.freeze({
            document: document,
            commands: commands,
            inspect: function(key) { return document[key]; },
            print: function(value) {
                const text = String(value);
                if (lines.join('\n').length + text.length > 4096)
                    throw Error('Output exceeds 4 KiB');
                lines.push(text);
            },
            command: function(id, args) {
                if (typeof id !== 'string' || !id || id.length > 128)
                    throw Error('Invalid command ID');
                if (actions.length >= 64) throw Error('Too many commands');
                if (args === undefined) args = {};
                if (!args || typeof args !== 'object' || Array.isArray(args))
                    throw Error('Command arguments must be an object');
                const serialized = JSON.stringify(args);
                if (serialized.length > 16384) throw Error('Command arguments exceed 16 KiB');
                actions.push({id: id, args: JSON.parse(serialized)});
                return actions.length;
            }
        });
        try {
            const value = (function() {
)JS");
    const QString suffix = QStringLiteral(R"JS(
            })();
            return {ok:true, output:lines.join('\n'), actions:actions,
                    result:value === undefined ? null : JSON.parse(JSON.stringify(value))};
        } catch (error) {
            return {ok:false, output:lines.join('\n'), error:String(error)};
        }
    })())JS");
    const QJSValue payload = engine.evaluate(prefix + source + suffix,
                                              QStringLiteral("script-console.js"));
    if (payload.isError()) evaluation.error = payload.toString();
    else {
        evaluation.output = payload.property(QStringLiteral("output")).toString();
        if (!payload.property(QStringLiteral("ok")).toBool())
            evaluation.error = payload.property(QStringLiteral("error")).toString();
        else {
            evaluation.result = payload.property(QStringLiteral("result")).toVariant();
            evaluation.commands = payload.property(QStringLiteral("actions")).toVariant().toList();
        }
    }
    {
        const std::lock_guard lock(state->mutex);
        state->engine = nullptr;
    }
    return evaluation;
}

void ScriptRuntime::finishEvaluation(quint64 generation, Evaluation evaluation)
{
    if (generation != m_generation) return;
    m_execution.reset();
    m_output = evaluation.output;
    m_result = evaluation.result;
    if (!m_cancelReason.isEmpty()) finishFailure(m_cancelReason);
    else if (!evaluation.error.isEmpty()) finishFailure(evaluation.error);
    else {
        if (runCommands(evaluation.commands)) {
            m_recordedCommands = evaluation.commands;
            m_running = false;
            m_status = QStringLiteral("completed");
            emit changed();
            emit finished(true);
        }
    }
}

bool ScriptRuntime::runCommands(const QVariantList &commands)
{
    if (commands.size() > 64) {
        finishFailure(QStringLiteral("Too many commands"));
        return false;
    }
    if (commands.isEmpty()) return true;
    if (!m_registry) {
        finishFailure(QStringLiteral("No command registry is attached"));
        return false;
    }
    // A multi-command script needs an active semantic history. We reject it
    // before mutation if no rollback seam exists.
    if (commands.size() > 1 && (!m_undo || !m_undo->activeModel())) {
        finishFailure(QStringLiteral("Multiple commands require an active UndoService model"));
        return false;
    }
    for (const QVariant &item : commands) {
        const QVariantMap row = item.toMap();
        const QString id = row.value(QStringLiteral("id")).toString();
        const QJsonDocument arguments = QJsonDocument::fromVariant(
            row.value(QStringLiteral("args")));
        if (id.isEmpty() || !m_exposedCommandIds.contains(id)
            || !arguments.isObject() || arguments.toJson(QJsonDocument::Compact).size() > 16384
            || !m_registry->resolve(id).enabled) {
            finishFailure(QStringLiteral("Command is not exposed or unavailable: %1").arg(id));
            return false;
        }
    }
    const bool macro = commands.size() > 1;
    if (macro && !m_undo->beginMacro(QStringLiteral("Script commands"))) {
        finishFailure(QStringLiteral("Could not begin script undo group"));
        return false;
    }
    for (const QVariant &item : commands) {
        if (!m_cancelReason.isEmpty()) break;
        const QVariantMap row = item.toMap();
        const QString id = row.value(QStringLiteral("id")).toString();
        const QVariantMap arguments = QJsonDocument::fromVariant(
            row.value(QStringLiteral("args"))).object().toVariantMap();
        const auto error = m_registry->invoke(id, arguments);
        if (error) {
            if (macro && !m_undo->cancelMacro())
                finishFailure(QStringLiteral("%1; undo rollback failed").arg(error.message));
            else finishFailure(error.message);
            return false;
        }
    }
    if (!m_cancelReason.isEmpty()) {
        if (macro && !m_undo->cancelMacro())
            finishFailure(QStringLiteral("%1; undo rollback failed").arg(m_cancelReason));
        else finishFailure(m_cancelReason);
        return false;
    }
    if (macro && !m_undo->endMacro()) {
        finishFailure(QStringLiteral("Could not finish script undo group"));
        return false;
    }
    return true;
}

void ScriptRuntime::cancelWithReason(const QString &reason)
{
    if (!m_running) return;
    m_cancelReason = reason;
    if (!m_execution) return;
    m_execution->cancelled.store(true);
    const std::lock_guard lock(m_execution->mutex);
    if (m_execution->engine) m_execution->engine->setInterrupted(true);
}

void ScriptRuntime::cancel() { cancelWithReason(QStringLiteral("Script canceled")); }

bool ScriptRuntime::replay(const QVariantList &commands)
{
    if (m_running) return false;
    m_error.clear();
    m_cancelReason.clear();
    m_running = true;
    m_status = QStringLiteral("running");
    emit changed();
    if (!runCommands(commands)) return false;
    m_recordedCommands = commands;
    m_running = false;
    m_status = QStringLiteral("completed");
    emit changed();
    emit finished(true);
    return true;
}

bool ScriptRuntime::replayRecorded() { return replay(m_recordedCommands); }

QString ScriptRuntime::exportRecordedMacro() const
{
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("qindatk.command-macro")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("commands"),
                            QJsonArray::fromVariantList(m_recordedCommands)}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool ScriptRuntime::importRecordedMacro(const QString &json)
{
    if (m_running) return false;
    auto reject = [this](const QString &reason) {
        m_error = reason;
        emit changed();
        return false;
    };
    const QByteArray bytes = json.toUtf8();
    if (bytes.size() > 1100000) return reject(QStringLiteral("Macro exceeds size limit"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return reject(QStringLiteral("Invalid macro JSON"));
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("qindatk.command-macro")
        || root.value(QStringLiteral("version")).toInt(-1) != 1
        || !root.value(QStringLiteral("commands")).isArray())
        return reject(QStringLiteral("Unsupported macro format"));
    const QJsonArray commands = root.value(QStringLiteral("commands")).toArray();
    if (commands.size() > 64) return reject(QStringLiteral("Too many macro commands"));
    QVariantList accepted;
    for (const QJsonValue &value : commands) {
        if (!value.isObject()) return reject(QStringLiteral("Invalid macro command"));
        const QJsonObject row = value.toObject();
        const QJsonValue id = row.value(QStringLiteral("id"));
        const QJsonValue args = row.value(QStringLiteral("args"));
        if (!id.isString() || id.toString().isEmpty() || !args.isObject()
            || QJsonDocument(args.toObject()).toJson(QJsonDocument::Compact).size() > 16384)
            return reject(QStringLiteral("Invalid macro command"));
        accepted.append(QVariantMap{{QStringLiteral("id"), id.toString()},
                                    {QStringLiteral("args"), args.toObject().toVariantMap()}});
    }
    m_recordedCommands = accepted;
    m_error.clear();
    emit changed();
    return true;
}

void ScriptRuntime::clear()
{
    if (m_running) return;
    m_status = QStringLiteral("idle");
    m_output.clear();
    m_error.clear();
    m_result.clear();
    m_recordedCommands.clear();
    emit changed();
}

void ScriptRuntime::finishFailure(const QString &message)
{
    m_error = message;
    m_running = false;
    m_status = m_cancelReason.isEmpty() ? QStringLiteral("failed") : QStringLiteral("canceled");
    emit changed();
    emit finished(false);
}
} // namespace QindaTK
