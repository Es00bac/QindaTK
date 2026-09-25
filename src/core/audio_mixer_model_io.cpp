// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioMixerModel: parameters, shared bindings, channel meters and the
// session "mixer" part. Everything here is bounded by the live mix (WP-062,
// D-113): bindings and meters of channels that are gone are released, and a
// saved session leaves out what no reopened session could reach.
#include "audio_mixer_model.h"

#include "audio_channel_meters.h"
#include "audio_parameter_binding.h"
#include "audio_session.h"
#include "timeline_model.h"

namespace QindaTK {
namespace {

struct Parsed {
    AudioMixerContent content;
    QSet<QString> hidden;
};

std::optional<Parsed> parse(const QVariantMap &map, QString *error)
{
    const auto content = AudioMixer::fromMap(map, error);
    if (!content) return std::nullopt;
    Parsed parsed{*content, {}};
    for (const QString &id : map.value(QStringLiteral("hidden")).toStringList()) {
        if (!AudioMixer::isTrack(id) && !AudioMixer::bus(*content, id) && id != AudioMixer::masterId()) {
            *error = AudioMixerModel::tr("A hidden channel %1 is not part of the mix").arg(id);
            return std::nullopt;
        }
        parsed.hidden.insert(id);
    }
    return parsed;
}

bool isMixerKind(const QString &kind)
{
    static const QStringList kinds{QStringLiteral("gain"), QStringLiteral("pan"), QStringLiteral("width"),
                                   QStringLiteral("depth"), QStringLiteral("level")};
    return kinds.contains(kind);
}

} // namespace

QString AudioMixerModel::ensureParameter(const QString &owner, const QString &kind)
{
    QString name;
    QString channel;
    if (const AudioMixerSend *send = AudioMixer::isSend(owner) ? findSend(owner, &channel) : nullptr)
        name = tr("%1 → %2").arg(channelName(channel), channelName(send->target));
    else if (m_channels.contains(owner))
        name = channelName(owner);
    if (name.isEmpty()) {
        reject(tr("%1 is not a channel or send of this mix").arg(owner));
        return {};
    }
    const auto parameter = AudioMixer::parameterFor(owner, kind, name);
    if (!parameter) {
        reject(tr("%1 has no %2 parameter").arg(name, kind));
        return {};
    }
    if (m_automation->lane(parameter->id)) return parameter->id;
    // AGENT-NOTE: adding a parameter is not an automation history step, and
    // the mixer never removes one (removeParameter clears that history), so
    // undo can bring back a send or bus with its levels. A lane whose owner
    // is gone is left out of the saved session instead (keepsParameter).
    OperationError problem;
    if (!m_automation->addParameter(*parameter, &problem)) {
        reject(problem.message);
        return {};
    }
    return parameter->id;
}

AudioParameterBinding *AudioMixerModel::parameterBinding(const QString &owner, const QString &kind)
{
    const QString id = ensureParameter(owner, kind);
    if (id.isEmpty()) return nullptr;
    AudioParameterBinding *&binding = m_bindings[id];
    if (!binding) {
        binding = new AudioParameterBinding(this); // parented: QML never deletes it
        binding->setSession(m_session);
        binding->setParameterId(id);
    }
    return binding;
}

AudioMeterModel *AudioMixerModel::meter(const QString &channelId)
{
    return m_engine && m_channels.contains(channelId) ? m_engine->channelMeter(channelId) : nullptr;
}

AudioMeterModel *AudioMixerModel::watchMeter(const QString &channelId)
{
    if (channelId.isEmpty()) return nullptr;
    if (m_meterWatch[channelId]++ == 0 && m_channels.size() > AudioChannelMeterFrame::kMaxSlots)
        emit meterWatchChanged();
    return meter(channelId);
}

void AudioMixerModel::unwatchMeter(const QString &channelId)
{
    const auto it = m_meterWatch.find(channelId);
    if (it == m_meterWatch.end() || --it.value() > 0) return;
    m_meterWatch.erase(it);
    if (m_channels.size() > AudioChannelMeterFrame::kMaxSlots) emit meterWatchChanged();
}

QSet<QString> AudioMixerModel::watchedMeters() const
{
    QSet<QString> watched;
    for (auto it = m_meterWatch.cbegin(); it != m_meterWatch.cend(); ++it) watched.insert(it.key());
    return watched;
}

QString AudioMixerModel::meterProblem(const QString &channelId) const
{
    if (!m_channels.contains(channelId)) return tr("%1 is not a channel of this mix").arg(channelId);
    // The same choice AudioRenderGraph::compile makes (D-113).
    if (AudioMixer::meteredChannels(m_channels, watchedMeters(), AudioChannelMeterFrame::kMaxSlots).contains(channelId))
        return {};
    return tr("%1 is not metered: a mix meters %2 channels at once, the master and the strips on screen first")
        .arg(channelName(channelId))
        .arg(AudioChannelMeterFrame::kMaxSlots);
}

void AudioMixerModel::prune()
{
    // AGENT-GUARD (WP-062): a binding under a gesture stays until the gesture
    // ends; deleteLater, so a delegate that still names one reads null.
    for (auto it = m_bindings.begin(); it != m_bindings.end();) {
        const QString owner = it.key().section(QLatin1Char('/'), 0, 0);
        const bool live = m_channels.contains(owner) || (AudioMixer::isSend(owner) && findSend(owner));
        if (live || it.value()->gestureActive()) {
            ++it;
            continue;
        }
        it.value()->deleteLater();
        it = m_bindings.erase(it);
    }
    if (m_engine) m_engine->retainChannelMeters(QSet<QString>(m_channels.cbegin(), m_channels.cend()));
}

bool AudioMixerModel::liveTrack(const QString &channelId) const
{
    return AudioMixer::isTrack(channelId) && m_timeline->trackRecord(AudioMixer::trackOf(channelId));
}

bool AudioMixerModel::keepsParameter(const QString &parameterId) const
{
    const qsizetype slash = parameterId.lastIndexOf(QLatin1Char('/'));
    if (slash < 0 || !isMixerKind(parameterId.mid(slash + 1))) return true; // not a mixer parameter
    const QString owner = parameterId.left(slash);
    if (AudioMixer::isTrack(owner)) return liveTrack(owner);
    if (AudioMixer::isBus(owner)) return AudioMixer::bus(m_content, owner);
    if (AudioMixer::isSend(owner)) {
        QString channel;
        return findSend(owner, &channel) && (!AudioMixer::isTrack(channel) || liveTrack(channel));
    }
    return true; // the master, or an ID the mixer does not own
}

QVariantMap AudioMixerModel::contentMap() const
{
    QVariantMap map = AudioMixer::toMap(m_content);
    map.insert(QStringLiteral("hidden"), hiddenChannels());
    return map;
}

QVariantMap AudioMixerModel::savedContentMap() const
{
    // AGENT-NOTE (D-110): a deleted track's settings stay dormant for timeline
    // undo, but no history is saved, so a reopened session could never bring
    // them back; the file leaves them out and stays bounded by the live mix.
    AudioMixerContent content = m_content;
    for (auto it = content.strips.begin(); it != content.strips.end();)
        it = AudioMixer::isTrack(it.key()) && !liveTrack(it.key()) ? content.strips.erase(it) : std::next(it);
    for (AudioMixerGroup &group : content.groups)
        group.members.removeIf([this](const QString &id) { return AudioMixer::isTrack(id) && !liveTrack(id); });
    QStringList hidden = hiddenChannels();
    hidden.removeIf([this](const QString &id) { return AudioMixer::isTrack(id) && !liveTrack(id); });
    QVariantMap map = AudioMixer::toMap(content);
    map.insert(QStringLiteral("hidden"), hidden);
    return map;
}

QString AudioMixerModel::contentProblem(const QVariantMap &content)
{
    QString error;
    return parse(content, &error) ? QString() : error;
}

QVariantMap AudioMixerModel::loadContent(const QVariantMap &content)
{
    QString error;
    const auto parsed = parse(content, &error);
    if (!parsed) return reject(error);
    m_hidden = parsed->hidden;
    m_history.clear();
    m_cursor = 0;
    apply(parsed->content);
    emit historyChanged();
    emit contentReset();
    return accepted();
}

} // namespace QindaTK
