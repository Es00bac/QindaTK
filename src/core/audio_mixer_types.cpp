// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_mixer_types.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace QindaTK::AudioMixer {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AudioMixer", text);
}

QUuid uuidAfter(const QString &id, QLatin1StringView prefix)
{
    return id.startsWith(prefix) ? QUuid::fromString(QStringView(id).mid(prefix.size())) : QUuid();
}

bool isChannelOwner(const QString &owner)
{
    return isTrack(owner) || isBus(owner) || owner == masterId();
}

QString busName(const AudioMixerContent &content, const QString &id)
{
    if (id == masterId()) return tr("Master");
    const AudioMixerBus *found = bus(content, id);
    return found && !found->name.isEmpty() ? found->name : id;
}

} // namespace

QString masterId() { return QStringLiteral("master"); }
QString mainPortId() { return QStringLiteral("main"); }
QString schemaId() { return QStringLiteral("qindatk.audio-mixer/1"); }

QString trackChannelId(const QUuid &trackId)
{
    return QStringLiteral("track:") + trackId.toString(QUuid::WithoutBraces);
}

QUuid trackOf(const QString &channelId) { return uuidAfter(channelId, QLatin1StringView("track:")); }
bool isTrack(const QString &channelId) { return !trackOf(channelId).isNull(); }
bool isBus(const QString &channelId) { return !uuidAfter(channelId, QLatin1StringView("bus:")).isNull(); }
bool isSend(const QString &id) { return !uuidAfter(id, QLatin1StringView("send:")).isNull(); }
bool isGroup(const QString &id) { return !uuidAfter(id, QLatin1StringView("group:")).isNull(); }

QString newId(const QString &prefix)
{
    return prefix + QLatin1Char(':') + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString parameterId(const QString &owner, const QString &kind)
{
    return owner + QLatin1Char('/') + kind;
}

std::optional<AutomationParameter> parameterFor(const QString &owner, const QString &kind, const QString &name)
{
    AutomationParameter parameter;
    parameter.id = parameterId(owner, kind);
    const bool channel = isChannelOwner(owner);
    if ((kind == QLatin1String("gain") && channel) || (kind == QLatin1String("level") && isSend(owner))) {
        // The same descriptor AudioSession::ensureGainParameter gives a track.
        parameter.name = (kind == QLatin1String("gain") ? tr("%1 gain") : tr("%1 send level")).arg(name);
        parameter.range = {UnitDimension::Gain, QStringLiteral("dB"), -60.0, 12.0};
        parameter.defaultValue = 0.0;
    } else if (kind == QLatin1String("pan") && channel) {
        parameter.name = tr("%1 pan").arg(name);
        parameter.range = {UnitDimension::Scalar, QStringLiteral("normalized"), -1.0, 1.0};
        parameter.defaultValue = 0.0;
        parameter.step = 0.01;
    } else if (kind == QLatin1String("width") && channel) {
        parameter.name = tr("%1 width").arg(name);
        parameter.range = {UnitDimension::Scalar, QStringLiteral("normalized"), 0.0, 2.0};
        parameter.defaultValue = 1.0;
        parameter.step = 0.01;
    } else if (kind == QLatin1String("depth") && channel) {
        parameter.name = tr("%1 depth").arg(name);
        parameter.range = {UnitDimension::Scalar, QStringLiteral("normalized"), -1.0, 1.0};
        parameter.defaultValue = 1.0;
        parameter.step = 0.01;
    } else {
        return std::nullopt;
    }
    return parameter;
}

QString panLawName(AudioPanLaw law)
{
    switch (law) {
    case AudioPanLaw::Compromise: return QStringLiteral("-4.5dB");
    case AudioPanLaw::Linear: return QStringLiteral("-6dB");
    case AudioPanLaw::Balance: return QStringLiteral("0dB");
    case AudioPanLaw::ConstantPower: break;
    }
    return QStringLiteral("-3dB");
}

std::optional<AudioPanLaw> panLawFromName(const QString &name)
{
    for (AudioPanLaw law : {AudioPanLaw::ConstantPower, AudioPanLaw::Compromise, AudioPanLaw::Linear, AudioPanLaw::Balance})
        if (panLawName(law) == name) return law;
    return std::nullopt;
}

QStringList panLawNames() { return {QStringLiteral("-3dB"), QStringLiteral("-4.5dB"), QStringLiteral("-6dB"), QStringLiteral("0dB")}; }

void panGains(AudioPanLaw law, double pan, double &left, double &right) noexcept
{
    const double p = std::isfinite(pan) ? std::clamp(pan, -1.0, 1.0) : 0.0;
    if (p <= -1.0) {
        left = 1.0;
        right = 0.0;
        return;
    }
    if (p >= 1.0) {
        left = 0.0;
        right = 1.0;
        return;
    }
    // AGENT-NOTE: cos((1 + p) pi/4) and cos((1 - p) pi/4) instead of cos/sin of
    // one angle: sin(pi/4) and cos(pi/4) differ in the last bit, so the centre
    // would not be symmetric (docs/audio-mixer.md, D-111).
    const double quarter = std::numbers::pi / 4.0;
    switch (law) {
    case AudioPanLaw::Linear:
        left = (1.0 - p) * 0.5;
        right = (1.0 + p) * 0.5;
        return;
    case AudioPanLaw::Balance:
        balanceGains(p, left, right);
        return;
    case AudioPanLaw::Compromise:
        left = std::sqrt(std::cos((1.0 + p) * quarter) * (1.0 - p) * 0.5);
        right = std::sqrt(std::cos((1.0 - p) * quarter) * (1.0 + p) * 0.5);
        return;
    case AudioPanLaw::ConstantPower:
        break;
    }
    left = std::cos((1.0 + p) * quarter);
    right = std::cos((1.0 - p) * quarter);
}

void balanceGains(double pan, double &left, double &right) noexcept
{
    const double p = std::isfinite(pan) ? std::clamp(pan, -1.0, 1.0) : 0.0;
    left = std::min(1.0, 1.0 - p);
    right = std::min(1.0, 1.0 + p);
}

std::optional<Port> parsePort(const QString &id)
{
    if (id == mainPortId()) return Port{true, {}, 1, 2};
    if (!id.startsWith(QLatin1String("device:"))) return std::nullopt;
    const qsizetype at = id.indexOf(QLatin1Char('@'));
    if (at < 8 || at + 1 >= id.size()) return std::nullopt;
    const QString channels = id.mid(7, at - 7);
    const qsizetype dash = channels.indexOf(QLatin1Char('-'));
    bool firstOk = false;
    bool lastOk = true;
    Port port;
    port.deviceId = id.mid(at + 1);
    port.first = (dash < 0 ? channels : channels.left(dash)).toInt(&firstOk);
    const int last = dash < 0 ? port.first : channels.mid(dash + 1).toInt(&lastOk);
    if (!firstOk || !lastOk || port.first < 1 || !layoutForLanes(last - port.first + 1)) return std::nullopt;
    port.count = last - port.first + 1;
    return port;
}

QString devicePortId(const QString &deviceId, int first, int count)
{
    return count > 1 ? QStringLiteral("device:%1-%2@%3").arg(first).arg(first + count - 1).arg(deviceId)
                     : QStringLiteral("device:%1@%2").arg(first).arg(deviceId);
}

bool isPort(const QString &id) { return parsePort(id).has_value(); }

AudioMixerStrip defaultStrip(const QString &channelId)
{
    AudioMixerStrip strip;
    strip.output = channelId == masterId() ? mainPortId() : masterId();
    return strip;
}

AudioMixerStrip strip(const AudioMixerContent &content, const QString &channelId)
{
    const auto it = content.strips.constFind(channelId);
    return it == content.strips.cend() ? defaultStrip(channelId) : *it;
}

const AudioMixerBus *bus(const AudioMixerContent &content, const QString &busId)
{
    for (const AudioMixerBus &entry : content.buses)
        if (entry.id == busId) return &entry;
    return nullptr;
}

const AudioMixerGroup *group(const AudioMixerContent &content, const QString &groupId)
{
    for (const AudioMixerGroup &entry : content.groups)
        if (entry.id == groupId) return &entry;
    return nullptr;
}

void normalize(AudioMixerContent &content)
{
    for (auto it = content.strips.begin(); it != content.strips.end();)
        it = *it == defaultStrip(it.key()) ? content.strips.erase(it) : std::next(it);
}

QStringList destinationsOf(const AudioMixerContent &content, const QString &channelId)
{
    const AudioMixerStrip settings = strip(content, channelId);
    QStringList destinations;
    if (isBus(settings.output) || settings.output == masterId()) destinations.push_back(settings.output);
    for (const AudioMixerSend &send : settings.sends) destinations.push_back(send.target);
    return destinations;
}

QStringList loopThrough(const AudioMixerContent &content, const QString &from, const QString &to)
{
    if (from == to) return {from};
    // Only buses both receive and feed, so only a bus-to-bus route can close a loop.
    if (!isBus(from) || !isBus(to)) return {};
    QHash<QString, QString> parent{{to, QString()}};
    QStringList queue{to};
    for (qsizetype next = 0; next < queue.size(); ++next) {
        const QString current = queue.at(next);
        for (const QString &target : destinationsOf(content, current)) {
            if (!isBus(target) || parent.contains(target)) continue;
            parent.insert(target, current);
            if (target == from) {
                QStringList path{from};
                for (QString at = current; !at.isEmpty(); at = parent.value(at)) path.prepend(at);
                return path;
            }
            queue.push_back(target);
        }
    }
    return {};
}

QString validate(const AudioMixerContent &content)
{
    if (content.buses.size() > kMaxBuses) return tr("A mix holds at most %1 buses").arg(kMaxBuses);
    QSet<QString> buses;
    for (const AudioMixerBus &entry : content.buses) {
        if (!isBus(entry.id)) return tr("Bus IDs have the form bus:<uuid>");
        if (buses.contains(entry.id)) return tr("Bus %1 appears twice").arg(entry.id);
        buses.insert(entry.id);
    }
    const AudioMixerStrip master = strip(content, masterId());
    if (master.layout != AudioLayout::Stereo) return tr("The master bus is stereo");
    if (!master.output.isEmpty() && !isPort(master.output)) return tr("The master bus feeds only an output port");
    if (!master.sends.isEmpty() || master.soloSafe) return tr("The master bus has no sends and cannot be solo safe");
    QSet<QString> sends;
    for (auto it = content.strips.cbegin(); it != content.strips.cend(); ++it) {
        const QString &channel = it.key();
        const AudioMixerStrip &settings = *it;
        if (!isTrack(channel) && !buses.contains(channel) && channel != masterId())
            return tr("Routing names an unknown channel %1").arg(channel);
        if (isTrack(channel) && isSurround(settings.layout))
            return tr("Tracks are Mono or Stereo; surround layouts are for buses");
        const QString &output = settings.output;
        if (!output.isEmpty() && output != masterId() && !buses.contains(output) && !isPort(output))
            return tr("%1 feeds an unknown destination %2").arg(channel, output);
        if (output == channel) return tr("%1 cannot feed itself").arg(channel);
        if (settings.sends.size() > kMaxSends) return tr("A channel has at most %1 sends").arg(kMaxSends);
        QSet<QString> targets;
        for (const AudioMixerSend &send : settings.sends) {
            if (!isSend(send.id) || sends.contains(send.id)) return tr("Send IDs have the form send:<uuid> and are unique");
            sends.insert(send.id);
            if (!buses.contains(send.target)) return tr("A send feeds a bus; %1 is not one").arg(send.target);
            if (send.target == channel) return tr("%1 cannot send to itself").arg(busName(content, channel));
            if (targets.contains(send.target)) return tr("%1 sends to %2 twice").arg(channel, send.target);
            targets.insert(send.target);
        }
    }
    if (content.groups.size() > kMaxGroups) return tr("A mix holds at most %1 groups").arg(kMaxGroups);
    QSet<QString> groups;
    for (const AudioMixerGroup &entry : content.groups) {
        if (!isGroup(entry.id) || groups.contains(entry.id)) return tr("Group IDs have the form group:<uuid> and are unique");
        groups.insert(entry.id);
        const QSet<QString> members(entry.members.cbegin(), entry.members.cend());
        if (members.size() != entry.members.size()) return tr("Group %1 lists a channel twice").arg(entry.name);
        for (const QString &member : entry.members)
            if (!isTrack(member) && !buses.contains(member)) return tr("Group %1 holds an unknown channel %2").arg(entry.name, member);
    }
    // AGENT-GUARD (D-112): the bus graph stays acyclic; an operation that
    // would close a loop is refused with the loop, and a file with one is not loaded.
    for (const AudioMixerBus &entry : content.buses)
        for (const QString &target : destinationsOf(content, entry.id)) {
            const QStringList loop = loopThrough(content, entry.id, target);
            if (loop.isEmpty()) continue;
            QStringList names{busName(content, entry.id)};
            for (const QString &id : loop) names.push_back(busName(content, id));
            return tr("Routing loop: %1").arg(names.join(QStringLiteral(" → ")));
        }
    return {};
}

Audibility audibility(const AudioMixerContent &content, const QStringList &channels,
                      const QSet<QString> &trackMuted, const QSet<QString> &trackSoloed)
{
    Audibility result;
    const QSet<QString> present(channels.cbegin(), channels.cend());
    QSet<QString> muted;
    for (const QString &id : channels) {
        if (isTrack(id)) {
            if (trackMuted.contains(id)) muted.insert(id);
            if (trackSoloed.contains(id)) result.soloed.insert(id);
        } else if (const AudioMixerBus *entry = bus(content, id)) {
            if (entry->muted) muted.insert(id);
            if (entry->soloed) result.soloed.insert(id);
        } else if (id == masterId() && content.masterMuted) {
            muted.insert(id);
        }
    }
    for (const AudioMixerGroup &entry : content.groups)
        for (const QString &member : entry.members) {
            if (!present.contains(member)) continue;
            if (entry.muted) {
                muted.insert(member);
                result.groupMuted.insert(member);
            }
            if (entry.soloed) result.soloed.insert(member);
        }
    result.anySolo = !result.soloed.isEmpty();
    if (result.anySolo) {
        // Solo in place: a soloed channel keeps what feeds it (upstream) and
        // what it feeds (downstream), through outputs and sends alike.
        QHash<QString, QStringList> forward;
        QHash<QString, QStringList> backward;
        for (const QString &id : channels)
            for (const QString &target : destinationsOf(content, id))
                if (present.contains(target)) {
                    forward[id].push_back(target);
                    backward[target].push_back(id);
                }
        QSet<QString> audible = result.soloed;
        for (const QHash<QString, QStringList> *edges : {&forward, &backward}) {
            QStringList queue(result.soloed.cbegin(), result.soloed.cend());
            QSet<QString> seen = result.soloed;
            for (qsizetype next = 0; next < queue.size(); ++next)
                for (const QString &neighbour : edges->value(queue.at(next)))
                    if (!seen.contains(neighbour)) {
                        seen.insert(neighbour);
                        audible.insert(neighbour);
                        queue.push_back(neighbour);
                    }
        }
        for (const QString &id : channels)
            if (!audible.contains(id) && id != masterId() && !strip(content, id).soloSafe) result.implicit.insert(id);
    }
    result.muted = muted | result.implicit;
    return result;
}

QSet<QString> meteredChannels(const QStringList &channels, const QSet<QString> &first, int slotCount)
{
    // Never `slots`: a Qt keyword macro.
    if (channels.size() <= slotCount) return QSet<QString>(channels.cbegin(), channels.cend());
    QSet<QString> chosen;
    if (channels.contains(masterId()) && slotCount > 0) chosen.insert(masterId());
    for (const QString &id : channels)
        if (chosen.size() < slotCount && first.contains(id)) chosen.insert(id);
    for (const QString &id : channels)
        if (chosen.size() < slotCount) chosen.insert(id);
    return chosen;
}

} // namespace QindaTK::AudioMixer
