// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_graph.h"

#include "audio_channel_meters.h"
#include "timeline_model.h"

#include <QCoreApplication>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace QindaTK {
namespace {

std::atomic<quint64> s_generations{0};

bool isAudio(const QString &kind)
{
    return kind.isEmpty() || kind == QLatin1String("audio");
}

// Tracks that become mixer channels: audio and MIDI (WP-064).
bool isChannelTrack(const QString &kind)
{
    return isAudio(kind) || kind == QLatin1String("midi");
}

QString tr(const char *text)
{
    return QCoreApplication::translate("AudioMixer", text);
}

// Cubic segments go through CurveEvaluator, which builds a curve per call;
// the engine instead reads kCubicSteps linear pieces evaluated here.
QVector<AutomationPoint> flattened(const AutomationParameter &parameter, const QVector<AutomationPoint> &points)
{
    QVector<AutomationPoint> out;
    for (qsizetype i = 0; i < points.size(); ++i) {
        const AutomationPoint &point = points.at(i);
        if (point.interpolation != CurveInterpolation::CubicBezier || i + 1 == points.size()) {
            AutomationPoint copy = point;
            if (copy.interpolation == CurveInterpolation::CubicBezier) copy.interpolation = CurveInterpolation::Linear;
            out.push_back(copy);
            continue;
        }
        const auto span = TimelineTime::subtract(points.at(i + 1).ticks, point.ticks);
        for (int step = 0; span && step < AudioRenderGraph::kCubicSteps; ++step) {
            const auto offset = TimelineTime::mulDiv(*span, quint64(step), AudioRenderGraph::kCubicSteps, TimeMap::Floor);
            AutomationPoint piece;
            piece.id = point.id;
            piece.ticks = point.ticks + offset.value_or(0);
            piece.value = AutomationCurve::valueAt(parameter, points, piece.ticks);
            piece.interpolation = CurveInterpolation::Linear;
            if (out.isEmpty() || out.last().ticks < piece.ticks) out.push_back(piece);
        }
    }
    return out;
}

// Buses in dependency order (a bus after every bus that feeds it); mixer
// order among independent buses. validate() refuses loops, so a bus left
// over here is only possible for content that bypassed it: it stays silent.
QStringList sortedBuses(const AudioMixerContent &mix, QStringList &problems)
{
    QHash<QString, int> incoming;
    QHash<QString, QStringList> edges;
    for (const AudioMixerBus &bus : mix.buses) incoming.insert(bus.id, 0);
    for (const AudioMixerBus &bus : mix.buses)
        for (const QString &target : AudioMixer::destinationsOf(mix, bus.id))
            if (incoming.contains(target) && target != bus.id) {
                edges[bus.id].push_back(target);
                ++incoming[target];
            }
    QStringList ready;
    for (const AudioMixerBus &bus : mix.buses)
        if (incoming.value(bus.id) == 0) ready.push_back(bus.id);
    for (qsizetype next = 0; next < ready.size(); ++next)
        for (const QString &target : edges.value(ready.at(next)))
            if (--incoming[target] == 0) ready.push_back(target);
    if (ready.size() != mix.buses.size()) problems.push_back(tr("Buses on a routing loop are silent"));
    return ready;
}

} // namespace

QString AudioRenderGraph::gainParameterId(const QString &trackId)
{
    return QStringLiteral("track:%1/gain").arg(trackId);
}

bool AudioRenderGraph::hasSilenceFloor(const AutomationParameter &parameter)
{
    return parameter.range.dimension == UnitDimension::Gain
        && parameter.range.unitId.compare(QLatin1String("dB"), Qt::CaseInsensitive) == 0;
}

int AudioRenderGraph::clipCount() const
{
    int count = 0;
    for (const Node &node : m_nodes) count += int(node.clips.size());
    return count;
}

double AudioRenderGraph::Curve::convert(double value) const noexcept
{
    return !decibels ? value : value <= silenceFloor ? 0.0 : std::pow(10.0, value / 20.0);
}

int AudioRenderGraph::addCurve(const EngineScheduleSnapshot *schedule, const QString &parameterId)
{
    const EngineScheduleSnapshot::Parameter *entry = schedule ? schedule->parameter(parameterId) : nullptr;
    if (!entry) return -1;
    Curve curve;
    curve.parameter = entry->parameter;
    curve.decibels = entry->parameter.range.unitId.compare(QLatin1String("dB"), Qt::CaseInsensitive) == 0;
    if (curve.decibels && hasSilenceFloor(entry->parameter)) curve.silenceFloor = entry->parameter.range.minimum;
    if (entry->overriding) {
        curve.constant = curve.convert(entry->liveValue);
    } else if (entry->points.isEmpty() || entry->mode == AutomationMode::Off) {
        curve.constant = curve.convert(entry->parameter.defaultValue);
    } else {
        curve.points = flattened(entry->parameter, entry->points);
        curve.automated = true;
    }
    m_curves.push_back(std::move(curve));
    return int(m_curves.size()) - 1;
}

AudioRenderGraph::Route AudioRenderGraph::converted(const Node &node, Route route) const
{
    // A surround source reaches every destination through a fixed matrix (D-111).
    const auto from = AudioMixer::layoutForLanes(node.lanes);
    const int destLanes = route.node >= 0 ? m_nodes[std::size_t(route.node)].lanes : m_ports[std::size_t(route.port)].lanes;
    const auto to = AudioMixer::layoutForLanes(destLanes);
    if (from && to && AudioMixer::isSurround(*from)) AudioMixer::surroundMatrix(*from, *to, route.matrix.data());
    return route;
}

int AudioRenderGraph::portIndex(const QString &id, const Sources &sources, const QString &from)
{
    for (std::size_t p = 0; p < m_ports.size(); ++p)
        if (m_ports[p].id == id) return int(p);
    const auto port = AudioMixer::parsePort(id);
    PortTarget target;
    target.id = id;
    if (port && port->main) {
        target.main = true;
    } else if (port && !sources.offline && !sources.deviceId.isEmpty() && port->deviceId == sources.deviceId
               && port->first + port->count - 1 <= sources.deviceChannels) {
        target.channel = port->first - 1;
        target.lanes = port->count;
    } else {
        // AGENT-GUARD (D-112): an unavailable port plays nothing; no other
        // port or channel of another device is chosen in its place.
        m_problems.push_back(tr("%1 feeds %2, which is not available").arg(from, id));
        return -1;
    }
    target.buffer = m_lanes;
    m_lanes += std::size_t(target.lanes);
    m_ports.push_back(target);
    return int(m_ports.size()) - 1;
}

std::shared_ptr<const AudioRenderGraph> AudioRenderGraph::compile(const Sources &sources)
{
    std::shared_ptr<AudioRenderGraph> graph(new AudioRenderGraph);
    graph->m_generation = ++s_generations;
    graph->m_sampleRate = sources.sampleRate;
    graph->m_schedule = sources.schedule;
    if (sources.sampleRate < 1) return graph;
    const AudioMixerContent mix = sources.mixer ? *sources.mixer : AudioMixerContent{};
    graph->m_panLaw = mix.panLaw;
    const EngineScheduleSnapshot *schedule = sources.schedule.get();
    const TimelineModel *timeline = sources.timeline;

    QStringList channels;
    QSet<QString> muted, soloed;
    if (timeline) {
        graph->m_timebase = timeline->timebase();
        for (const QUuid &id : timeline->trackOrder()) {
            const TimelineTrack *record = timeline->trackRecord(id);
            if (!record || !isChannelTrack(record->kind)) continue;
            const QString channel = AudioMixer::trackChannelId(id);
            channels.push_back(channel);
            if (record->muted) muted.insert(channel);
            if (record->solo) soloed.insert(channel);
        }
    }
    const qsizetype trackCount = channels.size();
    channels += sortedBuses(mix, graph->m_problems);
    channels.push_back(AudioMixer::masterId());
    const AudioMixer::Audibility audibility = AudioMixer::audibility(mix, channels, muted, soloed);
    // AGENT-GUARD (D-113): slots go by what is watched, never by processing
    // order alone, so the master and the strips on screen are always fed.
    QStringList mixerOrder = channels.mid(0, trackCount);
    for (const AudioMixerBus &bus : mix.buses) mixerOrder.push_back(bus.id);
    mixerOrder.push_back(AudioMixer::masterId());
    const QSet<QString> metered = AudioMixer::meteredChannels(mixerOrder, sources.meterFirst,
                                                              AudioChannelMeterFrame::kMaxSlots);
    QHash<QString, int> index;
    for (int i = 0; i < int(channels.size()); ++i) index.insert(channels.at(i), i);

    graph->m_nodes.resize(std::size_t(channels.size()));
    for (int i = 0; i < int(channels.size()); ++i) {
        Node &node = graph->m_nodes[std::size_t(i)];
        const QString &id = channels.at(i);
        node.channelId = id;
        node.lanes = id == AudioMixer::masterId() ? 2 : int(AudioMixer::strip(mix, id).layout);
        node.audible = !audibility.muted.contains(id);
        node.buffer = graph->m_lanes;
        graph->m_lanes += std::size_t(node.lanes);
        node.gain = graph->addCurve(schedule, AudioMixer::parameterId(id, QStringLiteral("gain")));
        node.pan = graph->addCurve(schedule, AudioMixer::parameterId(id, QStringLiteral("pan")));
        node.width = graph->addCurve(schedule, AudioMixer::parameterId(id, QStringLiteral("width")));
        node.depth = graph->addCurve(schedule, AudioMixer::parameterId(id, QStringLiteral("depth")));
        if (metered.contains(id)) {
            node.meterSlot = int(graph->m_meterChannels.size());
            graph->m_meterChannels.push_back(id);
            graph->m_meterLanes.push_back(node.lanes);
        }
    }

    // Clips of the tracks that play; a silent track neither plays nor
    // reports missing media, as before the mixer existed.
    const TimelineRate rate{sources.sampleRate, 1};
    const auto toSamples = [&](qint64 ticks) {
        return TimelineTime::rescale(ticks, graph->m_timebase, rate, TimeMap::NearestAway);
    };
    for (qsizetype i = 0; i < trackCount; ++i) {
        Node &node = graph->m_nodes[std::size_t(i)];
        if (!node.audible) continue;
        for (const auto &entry : timeline->trackIndex(AudioMixer::trackOf(node.channelId))) {
            const TimelineClip *clip = timeline->clipRecord(entry.id);
            if (!clip || clip->resourceId.isEmpty() || !isAudio(clip->kind)) continue;
            if (isCompResource(clip->resourceId)) {
                graph->addComp(node, *clip, sources); // WP-065 take comps
                continue;
            }
            AudioBufferPtr media = sources.media ? sources.media(clip->resourceId, sources.sampleRate) : nullptr;
            if (!media || media->frames == 0 || media->channels < 1) {
                if (!graph->m_missing.contains(clip->resourceId)) graph->m_missing.push_back(clip->resourceId);
                continue;
            }
            const auto start = toSamples(clip->start);
            const auto end = toSamples(clip->end());
            const auto offset = toSamples(clip->sourceOffset);
            if (!start || !end || !offset || *end <= *start) continue;
            node.clips.push_back({*start, *end, *offset, std::move(media)});
            graph->m_length = std::max(graph->m_length, *end);
        }
        std::sort(node.clips.begin(), node.clips.end(), [](const Clip &a, const Clip &b) { return a.start < b.start; });
    }

    graph->compileMidi(sources, trackCount);

    // Routes: the output, then the sends. Destinations always come later in
    // the processing order, so one pass mixes the whole graph.
    for (int i = 0; i < int(channels.size()); ++i) {
        Node &node = graph->m_nodes[std::size_t(i)];
        const QString &id = node.channelId;
        const AudioMixerStrip settings = AudioMixer::strip(mix, id);
        QString output = settings.output;
        if (id == AudioMixer::masterId() && sources.offline) output = AudioMixer::mainPortId();
        if (!output.isEmpty()) {
            Route route;
            if (index.value(output, -1) > i) route.node = index.value(output);
            else if (AudioMixer::isPort(output)) route.port = graph->portIndex(output, sources, id);
            if (route.node >= 0 || route.port >= 0) node.routes.push_back(graph->converted(node, route));
        }
        for (const AudioMixerSend &send : settings.sends) {
            if (index.value(send.target, -1) <= i) continue;
            Route route;
            route.node = index.value(send.target);
            route.preFader = send.preFader;
            route.level = graph->addCurve(schedule, AudioMixer::parameterId(send.id, QStringLiteral("level")));
            node.routes.push_back(graph->converted(node, route));
        }
    }
    graph->m_work.assign(graph->m_lanes * std::size_t(kChunk), 0.0);
    return graph;
}

} // namespace QindaTK
