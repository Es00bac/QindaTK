// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_parameter_binding.h"

#include "audio_graph.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {

AudioParameterBinding::AudioParameterBinding(QObject *parent)
    : QObject(parent), m_mapper(new AudioParameterMap(this))
{
}

AudioParameterBinding::~AudioParameterBinding()
{
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
    // A gesture left open (its control was destroyed mid-drag) is released,
    // never committed: the engine returns to the committed value.
    if (m_active) release();
}

AutomationModel *AudioParameterBinding::automation() const
{
    return m_session ? m_session->automation() : nullptr;
}

const AutomationModel::Lane *AudioParameterBinding::lane() const
{
    const AutomationModel *model = automation();
    return model && !m_parameterId.isEmpty() ? model->lane(m_parameterId) : nullptr;
}

qint64 AudioParameterBinding::ticks() const
{
    return m_session ? m_session->transportTicks() : 0;
}

void AudioParameterBinding::setSession(AudioSession *session)
{
    if (m_session == session) return;
    if (m_active) release();
    m_session = session;
    reconnect();
    emit sessionChanged();
}

void AudioParameterBinding::setParameterId(const QString &parameterId)
{
    if (m_parameterId == parameterId) return;
    if (m_active) release();
    m_parameterId = parameterId;
    refresh();
    emit parameterIdChanged();
}

void AudioParameterBinding::reconnect()
{
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
    m_connections.clear();
    if (AudioSession *session = m_session) {
        // Every committed change, mode change and live gesture value reaches
        // the engine as a new schedule snapshot; the binding reads the same one.
        m_connections << connect(session->schedule(), &EngineSchedule::published, this, &AudioParameterBinding::refresh)
                      << connect(session->automation(), &AutomationModel::parametersChanged, this,
                                 &AudioParameterBinding::refresh)
                      << connect(session, &AudioSession::engineChanged, this, &AudioParameterBinding::reconnect)
                      << connect(session, &QObject::destroyed, this, [this] {
                             m_active = false;
                             refresh();
                         });
        // The transport position moves a curve's value under the control.
        if (AudioEngine *engine = session->engine())
            m_connections << connect(engine->transport(), &TransportModel::changed, this,
                                     &AudioParameterBinding::refreshValue);
        else
            m_connections << connect(session->automation(), &AutomationModel::transportChanged, this,
                                     &AudioParameterBinding::refreshValue);
    }
    refresh();
}

void AudioParameterBinding::refresh()
{
    refreshDescriptor();
    refreshAutomation();
    refreshValue();
}

void AudioParameterBinding::refreshDescriptor()
{
    const AutomationModel::Lane *found = lane();
    const bool valid = found != nullptr;
    const AutomationParameter parameter = found ? found->parameter : AutomationParameter{};
    if (valid == m_valid && parameter == m_parameter) return;
    m_valid = valid;
    m_parameter = parameter;
    updateMapper();
    emit descriptorChanged();
}

void AudioParameterBinding::refreshAutomation()
{
    const AutomationModel::Lane *found = lane();
    const QString mode = found ? AutomationCodec::modeName(found->mode) : QString();
    const bool touched = found && found->touched;
    const bool writing = found && found->writing;
    const bool latched = found && found->latched;
    if (mode == m_mode && touched == m_touched && writing == m_writing && latched == m_latched) return;
    m_mode = mode;
    m_touched = touched;
    m_writing = writing;
    m_latched = latched;
    emit automationChanged();
}

void AudioParameterBinding::refreshValue()
{
    double value = 0.0;
    bool automated = false;
    if (m_valid && m_session) {
        const EngineScheduleSnapshotPtr snapshot = m_session->schedule()->acquire();
        const EngineScheduleSnapshot::Value current =
            snapshot ? snapshot->valueAtTicks(m_parameterId, ticks()) : EngineScheduleSnapshot::Value{};
        value = current.known ? current.value : m_parameter.defaultValue;
        automated = current.automated;
    }
    const bool silent = silenceFloor() && value <= m_parameter.range.minimum;
    if (value == m_value && silent == m_silent && automated == m_automated) return;
    m_value = value;
    m_silent = silent;
    m_automated = automated;
    emit valueChanged();
}

bool AudioParameterBinding::silenceFloor() const
{
    return m_valid && AudioRenderGraph::hasSilenceFloor(m_parameter);
}

QString AudioParameterBinding::unit() const
{
    return m_parameter.range.unitId == QLatin1String("normalized") ? QString() : m_parameter.range.unitId;
}

QString AudioParameterBinding::taper() const
{
    const CurveAxisDomain &range = m_parameter.range;
    if (!m_valid) return {};
    if (silenceFloor() && range.minimum < 0 && range.maximum > 0) return QStringLiteral("gainDb");
    if (range.dimension == UnitDimension::Frequency && range.minimum > 0) return QStringLiteral("logarithmic");
    return QStringLiteral("linear");
}

void AudioParameterBinding::updateMapper()
{
    if (!m_valid) return;
    const CurveAxisDomain &range = m_parameter.range;
    QVariantMap descriptor{{QStringLiteral("taper"), taper()},
                           {QStringLiteral("minimum"), range.minimum},
                           {QStringLiteral("maximum"), range.maximum}};
    if (taper() == QLatin1String("gainDb")) descriptor.insert(QStringLiteral("unityPosition"), m_unityPosition);
    m_mapper->setDescriptor(descriptor);
}

double AudioParameterBinding::resetValue() const
{
    if (!std::isnan(m_resetValue)) return m_resetValue;
    return m_valid ? std::clamp(0.0, m_parameter.range.minimum, m_parameter.range.maximum) : 0.0;
}

void AudioParameterBinding::setResetValue(double value)
{
    if (value == m_resetValue || (std::isnan(value) && std::isnan(m_resetValue))) return;
    m_resetValue = value;
    emit descriptorChanged();
}

void AudioParameterBinding::setUnityPosition(double position)
{
    if (!std::isfinite(position) || position <= 0.0 || position >= 1.0) {
        fail(tr("The unity position must lie inside the travel"));
        return;
    }
    if (position == m_unityPosition) return;
    m_unityPosition = position;
    updateMapper();
    emit descriptorChanged();
}

QString AudioParameterBinding::automationState() const
{
    if (!m_valid) return {};
    QString state = m_mode;
    if (m_writing) state += tr(", writing");
    else if (m_touched) state += tr(", held");
    if (m_latched) state += tr(", latched");
    return state;
}

double AudioParameterBinding::canonical(double value, bool silent) const
{
    if (!m_valid) return value;
    if (silent && silenceFloor()) return m_parameter.range.minimum;
    return AutomationCurve::quantized(m_parameter, value);
}

QString AudioParameterBinding::format(double value, bool silent) const
{
    if (!m_valid) return {};
    const QString suffix = unit().isEmpty() ? QString() : QLatin1Char(' ') + unit();
    if ((silent || value <= m_parameter.range.minimum) && silenceFloor()) return QStringLiteral("−∞") + suffix;
    const AutomationModel *model = automation();
    return model ? model->formatValue(m_parameterId, value) : QString::number(value) + suffix;
}

bool AudioParameterBinding::begin(double value, bool silent)
{
    if (!m_valid || !automation()) return fail(tr("No engine parameter is bound"));
    if (!silent && !std::isfinite(value)) return fail(tr("The value is not a number"));
    if (m_active) return preview(value, silent);
    const AutomationModel::Lane *found = lane();
    if (!found) return fail(tr("No engine parameter is bound"));
    m_wasWriting = found->writing;
    m_startValue = m_value; // what the engine plays now
    if (const OperationError error = automation()->beginGesture(m_parameterId, ticks(), canonical(value, silent)))
        return fail(error.message);
    m_active = true;
    clearError();
    emit gestureChanged();
    return true;
}

bool AudioParameterBinding::preview(double value, bool silent)
{
    if (!m_active) return begin(value, silent);
    if (!silent && !std::isfinite(value)) return fail(tr("The value is not a number"));
    if (const OperationError error = automation()->recordGesture(m_parameterId, ticks(), canonical(value, silent)))
        return fail(error.message);
    return true;
}

bool AudioParameterBinding::commit(double value, bool silent)
{
    if (!m_active && !begin(value, silent)) return false;
    AutomationModel *model = automation();
    const double target = canonical(value, silent);
    model->recordGesture(m_parameterId, ticks(), target);
    std::optional<AutomationBatchEdit> edit;
    QString result = plan(target, edit);
    if (result == QLatin1String("rejected")) {
        m_active = false;
        const bool ok = fail(tr("No engine parameter is bound"));
        finish(result);
        return ok;
    }
    bool ok = true;
    if (edit) {
        // AGENT-GUARD: edited while the control still holds the live value
        // and released afterwards, so the device never renders the old
        // static value between the two snapshots.
        const QString label = tr("Set %1").arg(lane()->parameter.name);
        if (const OperationError error = model->applyBatch({*edit}, label)) {
            ok = fail(error.message);
            result = QStringLiteral("rejected");
        }
    }
    model->endGesture(m_parameterId, ticks());
    m_active = false;
    if (ok) clearError();
    finish(result);
    return ok;
}

bool AudioParameterBinding::commitWith(double value, AudioParameterBinding *other, double otherValue)
{
    if (!other || other == this || other->automation() != automation()) {
        const bool ok = commit(value);
        return other && other != this ? other->commit(otherValue) && ok : ok;
    }
    if (!m_active && !begin(value)) return false;
    if (!other->m_active && !other->begin(otherValue)) {
        cancel();
        return false;
    }
    AutomationModel *model = automation();
    const double target = canonical(value);
    const double otherTarget = other->canonical(otherValue);
    model->recordGesture(m_parameterId, ticks(), target);
    model->recordGesture(other->m_parameterId, ticks(), otherTarget);
    std::optional<AutomationBatchEdit> edit, otherEdit;
    QString result = plan(target, edit);
    QString otherResult = other->plan(otherTarget, otherEdit);
    QVector<AutomationBatchEdit> edits;
    if (edit) edits.push_back(*edit);
    if (otherEdit) edits.push_back(*otherEdit);
    bool ok = result != QLatin1String("rejected") && otherResult != QLatin1String("rejected");
    if (ok && !edits.isEmpty()) {
        // AGENT-GUARD (D-100): one gesture over two parameters is ONE history step.
        const QString label = tr("Set %1 and %2").arg(m_parameter.name, other->m_parameter.name);
        if (const OperationError error = model->applyBatch(edits, label)) {
            ok = fail(error.message);
            if (edit) result = QStringLiteral("rejected");
            if (otherEdit) otherResult = QStringLiteral("rejected");
        }
    }
    model->endGesture(m_parameterId, ticks());
    model->endGesture(other->m_parameterId, ticks());
    m_active = false;
    other->m_active = false;
    if (ok) {
        clearError();
        other->clearError();
    }
    finish(result);
    other->finish(otherResult);
    return ok;
}

QString AudioParameterBinding::plan(double target, std::optional<AutomationBatchEdit> &edit) const
{
    const AutomationModel::Lane *found = lane();
    if (!found) return QStringLiteral("rejected");
    if (found->writing) return QStringLiteral("recorded"); // the pass is the transaction (touch: it ends on release)
    if (found->mode != AutomationMode::Off && !found->points.isEmpty()) return QStringLiteral("live");
    if (target == found->parameter.defaultValue) return QStringLiteral("unchanged");
    AutomationBatchEdit batch;
    batch.request.kind = AutomationEditKind::EditParameter;
    batch.request.parameterId = m_parameterId;
    batch.input.defaultValue = target;
    edit = batch;
    return QStringLiteral("static");
}

bool AudioParameterBinding::cancel()
{
    if (!m_active) return false;
    release();
    finish(QStringLiteral("canceled"));
    return true;
}

// Ends the open gesture without storing it. A pass this gesture opened is
// dropped; a pass that was already running (latch or write) gets the start
// value back from now on, since what it wrote was already heard.
void AudioParameterBinding::release()
{
    m_active = false;
    AutomationModel *model = automation();
    const AutomationModel::Lane *found = lane();
    if (!model || !found) return;
    if (found->writing && !m_wasWriting) {
        model->discardPass(m_parameterId);
        return;
    }
    model->recordGesture(m_parameterId, ticks(), m_startValue);
    model->endGesture(m_parameterId, ticks());
}

void AudioParameterBinding::finish(const QString &result)
{
    m_lastResult = result;
    refresh();
    emit gestureChanged();
    emit transactionFinished(result, m_value, m_silent);
}

bool AudioParameterBinding::setAutomationMode(const QString &mode)
{
    if (!m_valid || !automation()) return fail(tr("No engine parameter is bound"));
    const QVariantMap result = automation()->setModeName(m_parameterId, mode);
    if (!result.value(QStringLiteral("ok")).toBool()) return fail(result.value(QStringLiteral("error")).toString());
    clearError();
    return true;
}

bool AudioParameterBinding::fail(const QString &message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit errorChanged();
    }
    return false;
}

void AudioParameterBinding::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit errorChanged();
}

} // namespace QindaTK
