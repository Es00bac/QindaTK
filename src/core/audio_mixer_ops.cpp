// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioMixerModel operations: each validates, then records one history step
// (AudioMixerModel::commit) or changes the TimelineTrack that owns the flag.
#include "audio_mixer_model.h"

#include "timeline_model.h"

namespace QindaTK {

QVariantMap AudioMixerModel::applyTrackProperty(const QString &channelId, const QString &property, const QVariant &value)
{
    const QUuid track = AudioMixer::trackOf(channelId);
    if (track.isNull() || !m_timeline->trackRecord(track)) return reject(tr("%1 is not a track of this session").arg(channelId));
    TimelineEditRequest request;
    request.kind = TimelineEditKind::TrackProperty;
    request.trackId = track;
    request.property = property;
    request.value = value;
    if (const OperationError problem = m_timeline->apply(request, {})) return reject(problem.message);
    return accepted();
}

QVariantMap AudioMixerModel::addBus(const QString &name, const QString &layout)
{
    const auto parsed = AudioMixer::layoutFromName(layout);
    if (!parsed) return reject(tr("Unsupported layout “%1”: buses are Mono or Stereo").arg(layout));
    if (m_content.buses.size() >= AudioMixer::kMaxBuses) return reject(tr("A mix holds at most %1 buses").arg(AudioMixer::kMaxBuses));
    AudioMixerContent next = m_content;
    AudioMixerBus bus;
    bus.id = AudioMixer::newId(QStringLiteral("bus"));
    bus.name = name.trimmed().isEmpty() ? tr("Bus %1").arg(next.buses.size() + 1) : name.trimmed();
    next.buses.push_back(bus);
    AudioMixerStrip settings = AudioMixer::defaultStrip(bus.id);
    settings.layout = *parsed;
    next.strips.insert(bus.id, settings);
    return commit(tr("Add bus %1").arg(bus.name), next, {{QStringLiteral("channelId"), bus.id}});
}

QVariantMap AudioMixerModel::removeBus(const QString &busId)
{
    const AudioMixerBus *found = AudioMixer::bus(m_content, busId);
    if (!found) return reject(tr("%1 is not a bus of this mix").arg(busId));
    const QString name = found->name;
    AudioMixerContent next = m_content;
    next.buses.removeIf([&busId](const AudioMixerBus &bus) { return bus.id == busId; });
    next.strips.remove(busId);
    // AGENT-GUARD (D-112): what fed the bus is disconnected and reported,
    // never rerouted to another destination on its own.
    QStringList disconnected;
    for (auto it = next.strips.begin(); it != next.strips.end(); ++it) {
        if (it->output == busId) {
            it->output.clear();
            disconnected.push_back(it.key());
        }
        it->sends.removeIf([&busId](const AudioMixerSend &send) { return send.target == busId; });
    }
    for (AudioMixerGroup &group : next.groups) group.members.removeAll(busId);
    return commit(tr("Remove bus %1").arg(name), next, {{QStringLiteral("disconnected"), disconnected}});
}

QVariantMap AudioMixerModel::renameChannel(const QString &channelId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return reject(tr("A channel needs a name"));
    if (AudioMixer::isTrack(channelId)) return applyTrackProperty(channelId, QStringLiteral("name"), trimmed);
    if (!AudioMixer::bus(m_content, channelId)) return reject(tr("Only tracks and buses can be renamed"));
    AudioMixerContent next = m_content;
    for (AudioMixerBus &bus : next.buses)
        if (bus.id == channelId) bus.name = trimmed;
    return commit(tr("Rename %1").arg(channelName(channelId)), next);
}

QVariantMap AudioMixerModel::setLayout(const QString &channelId, const QString &layout)
{
    const auto parsed = AudioMixer::layoutFromName(layout);
    if (!parsed) return reject(tr("Unsupported layout “%1”: channels are Mono or Stereo").arg(layout));
    if (!m_channels.contains(channelId)) return reject(tr("%1 is not a channel of this mix").arg(channelId));
    if (channelId == AudioMixer::masterId() && *parsed != AudioLayout::Stereo) return reject(tr("The master bus is stereo"));
    AudioMixerContent next = m_content;
    AudioMixerStrip settings = AudioMixer::strip(next, channelId);
    settings.layout = *parsed;
    next.strips.insert(channelId, settings);
    return commit(tr("Make %1 %2").arg(channelName(channelId), layout), next);
}

QVariantMap AudioMixerModel::setOutput(const QString &channelId, const QString &destinationId)
{
    if (!m_channels.contains(channelId)) return reject(tr("%1 is not a channel of this mix").arg(channelId));
    if (const QString problem = routeProblem(channelId, destinationId); !problem.isEmpty()) return reject(problem);
    AudioMixerContent next = m_content;
    AudioMixerStrip settings = AudioMixer::strip(next, channelId);
    settings.output = destinationId;
    next.strips.insert(channelId, settings);
    return commit(tr("Route %1 to %2").arg(channelName(channelId), destinationLabel(destinationId)), next);
}

QVariantMap AudioMixerModel::addSend(const QString &channelId, const QString &busId, bool preFader)
{
    if (!m_channels.contains(channelId) || channelId == AudioMixer::masterId())
        return reject(tr("%1 has no sends").arg(channelName(channelId)));
    if (!AudioMixer::bus(m_content, busId)) return reject(tr("A send feeds a bus; %1 is not one").arg(channelName(busId)));
    if (busId == channelId) return reject(tr("%1 cannot send to itself").arg(channelName(channelId)));
    AudioMixerStrip settings = AudioMixer::strip(m_content, channelId);
    for (const AudioMixerSend &send : std::as_const(settings.sends))
        if (send.target == busId) return reject(tr("%1 already sends to %2").arg(channelName(channelId), channelName(busId)));
    if (settings.sends.size() >= AudioMixer::kMaxSends) return reject(tr("A channel has at most %1 sends").arg(AudioMixer::kMaxSends));
    const QStringList loop = AudioMixer::loopThrough(m_content, channelId, busId);
    if (!loop.isEmpty())
        return reject(tr("A send from %1 to %2 would close a feedback loop: %3")
                          .arg(channelName(channelId), channelName(busId), loopText(channelId, loop)));
    AudioMixerSend send;
    send.id = AudioMixer::newId(QStringLiteral("send"));
    send.target = busId;
    send.preFader = preFader;
    settings.sends.push_back(send);
    AudioMixerContent next = m_content;
    next.strips.insert(channelId, settings);
    return commit(tr("Send %1 to %2").arg(channelName(channelId), channelName(busId)), next,
                  {{QStringLiteral("sendId"), send.id},
                   {QStringLiteral("parameterId"), AudioMixer::parameterId(send.id, QStringLiteral("level"))}});
}

QVariantMap AudioMixerModel::removeSend(const QString &sendId)
{
    QString channelId;
    const AudioMixerSend *found = findSend(sendId, &channelId);
    if (!found) return reject(tr("%1 is not a send of this mix").arg(sendId));
    const QString target = found->target;
    AudioMixerContent next = m_content;
    next.strips[channelId].sends.removeIf([&sendId](const AudioMixerSend &send) { return send.id == sendId; });
    return commit(tr("Remove the send from %1 to %2").arg(channelName(channelId), channelName(target)), next);
}

QVariantMap AudioMixerModel::setSendPreFader(const QString &sendId, bool preFader)
{
    QString channelId;
    if (!findSend(sendId, &channelId)) return reject(tr("%1 is not a send of this mix").arg(sendId));
    AudioMixerContent next = m_content;
    for (AudioMixerSend &send : next.strips[channelId].sends)
        if (send.id == sendId) send.preFader = preFader;
    return commit(preFader ? tr("Send %1 before the fader").arg(channelName(channelId))
                           : tr("Send %1 after the fader").arg(channelName(channelId)),
                  next);
}

QVariantMap AudioMixerModel::setMuted(const QString &channelId, bool muted)
{
    if (AudioMixer::isTrack(channelId)) return applyTrackProperty(channelId, QStringLiteral("muted"), muted);
    AudioMixerContent next = m_content;
    if (channelId == AudioMixer::masterId()) {
        next.masterMuted = muted;
    } else if (AudioMixer::bus(next, channelId)) {
        for (AudioMixerBus &bus : next.buses)
            if (bus.id == channelId) bus.muted = muted;
    } else {
        return reject(tr("%1 is not a channel of this mix").arg(channelId));
    }
    return commit(muted ? tr("Mute %1").arg(channelName(channelId)) : tr("Unmute %1").arg(channelName(channelId)), next);
}

QVariantMap AudioMixerModel::setSoloed(const QString &channelId, bool soloed)
{
    if (AudioMixer::isTrack(channelId)) return applyTrackProperty(channelId, QStringLiteral("solo"), soloed);
    if (channelId == AudioMixer::masterId()) return reject(tr("The master bus cannot be soloed"));
    if (!AudioMixer::bus(m_content, channelId)) return reject(tr("%1 is not a channel of this mix").arg(channelId));
    AudioMixerContent next = m_content;
    for (AudioMixerBus &bus : next.buses)
        if (bus.id == channelId) bus.soloed = soloed;
    return commit(soloed ? tr("Solo %1").arg(channelName(channelId)) : tr("Unsolo %1").arg(channelName(channelId)), next);
}

QVariantMap AudioMixerModel::setArmed(const QString &channelId, bool armed)
{
    if (!AudioMixer::isTrack(channelId)) return reject(tr("Only tracks record"));
    return applyTrackProperty(channelId, QStringLiteral("armed"), armed);
}

QVariantMap AudioMixerModel::setSoloSafe(const QString &channelId, bool safe)
{
    if (channelId == AudioMixer::masterId()) return reject(tr("The master bus is never muted by a solo"));
    if (!m_channels.contains(channelId)) return reject(tr("%1 is not a channel of this mix").arg(channelId));
    AudioMixerContent next = m_content;
    AudioMixerStrip settings = AudioMixer::strip(next, channelId);
    settings.soloSafe = safe;
    next.strips.insert(channelId, settings);
    return commit(safe ? tr("Make %1 solo safe").arg(channelName(channelId))
                       : tr("Let a solo mute %1").arg(channelName(channelId)),
                  next);
}

QVariantMap AudioMixerModel::setHidden(const QString &channelId, bool hidden)
{
    if (!m_channels.contains(channelId)) return reject(tr("%1 is not a channel of this mix").arg(channelId));
    if (hidden == m_hidden.contains(channelId)) return accepted({{QStringLiteral("unchanged"), true}});
    if (hidden) m_hidden.insert(channelId);
    else m_hidden.remove(channelId);
    refresh(true);
    return accepted();
}

QVariantMap AudioMixerModel::addGroup(const QString &name, const QStringList &members)
{
    if (m_content.groups.size() >= AudioMixer::kMaxGroups) return reject(tr("A mix holds at most %1 groups").arg(AudioMixer::kMaxGroups));
    for (const QString &member : members)
        if (!m_channels.contains(member) || member == AudioMixer::masterId())
            return reject(tr("A group holds tracks and buses of this mix; %1 is not one").arg(member));
    AudioMixerGroup group;
    group.id = AudioMixer::newId(QStringLiteral("group"));
    group.name = name.trimmed().isEmpty() ? tr("Group %1").arg(m_content.groups.size() + 1) : name.trimmed();
    group.members = members;
    group.members.removeDuplicates();
    AudioMixerContent next = m_content;
    next.groups.push_back(group);
    return commit(tr("Add group %1").arg(group.name), next, {{QStringLiteral("groupId"), group.id}});
}

QVariantMap AudioMixerModel::removeGroup(const QString &groupId)
{
    const AudioMixerGroup *found = AudioMixer::group(m_content, groupId);
    if (!found) return reject(tr("%1 is not a group of this mix").arg(groupId));
    const QString name = found->name;
    AudioMixerContent next = m_content;
    next.groups.removeIf([&groupId](const AudioMixerGroup &group) { return group.id == groupId; });
    return commit(tr("Remove group %1").arg(name), next);
}

QVariantMap AudioMixerModel::setGroupMembers(const QString &groupId, const QStringList &members)
{
    if (!AudioMixer::group(m_content, groupId)) return reject(tr("%1 is not a group of this mix").arg(groupId));
    for (const QString &member : members)
        if (!m_channels.contains(member) || member == AudioMixer::masterId())
            return reject(tr("A group holds tracks and buses of this mix; %1 is not one").arg(member));
    AudioMixerContent next = m_content;
    QString name;
    for (AudioMixerGroup &group : next.groups)
        if (group.id == groupId) {
            group.members = members;
            group.members.removeDuplicates();
            name = group.name;
        }
    return commit(tr("Change the members of %1").arg(name), next);
}

QVariantMap AudioMixerModel::setGroupMuted(const QString &groupId, bool muted)
{
    if (!AudioMixer::group(m_content, groupId)) return reject(tr("%1 is not a group of this mix").arg(groupId));
    AudioMixerContent next = m_content;
    QString name;
    for (AudioMixerGroup &group : next.groups)
        if (group.id == groupId) {
            group.muted = muted;
            name = group.name;
        }
    return commit(muted ? tr("Mute group %1").arg(name) : tr("Unmute group %1").arg(name), next);
}

QVariantMap AudioMixerModel::setGroupSoloed(const QString &groupId, bool soloed)
{
    if (!AudioMixer::group(m_content, groupId)) return reject(tr("%1 is not a group of this mix").arg(groupId));
    AudioMixerContent next = m_content;
    QString name;
    for (AudioMixerGroup &group : next.groups)
        if (group.id == groupId) {
            group.soloed = soloed;
            name = group.name;
        }
    return commit(soloed ? tr("Solo group %1").arg(name) : tr("Unsolo group %1").arg(name), next);
}

QVariantMap AudioMixerModel::setPanLaw(const QString &law)
{
    const auto parsed = AudioMixer::panLawFromName(law);
    if (!parsed) return reject(tr("Unsupported pan law “%1”; use %2").arg(law, AudioMixer::panLawNames().join(QStringLiteral(", "))));
    AudioMixerContent next = m_content;
    next.panLaw = *parsed;
    return commit(tr("Use the %1 pan law").arg(law), next);
}

} // namespace QindaTK
