// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "automation_types.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <QVector>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (WP-062, docs/audio-mixer.md, D-110..D-112): plain mixer
// data shared by AudioMixerModel (GUI thread) and AudioRenderGraph::compile.
// Channel IDs: "track:<uuid>" (an audio timeline track), "bus:<uuid>" and
// "master". Parameter IDs: "<channel>/gain|pan|width" and "<send>/level".
// Output IDs: a bus, "master", a port ("main", "device:<n>[-<m>]@<device>")
// or "" (disconnected). Nothing here is realtime except the gain laws.
enum class AudioLayout { Mono = 1, Stereo = 2 };
enum class AudioPanLaw { ConstantPower, Compromise, Linear, Balance };

struct AudioMixerSend {
    QString id;            // "send:<uuid>"
    QString target;        // "bus:<uuid>"
    bool preFader = false; // pre: the send ignores the channel fader
    friend bool operator==(const AudioMixerSend &, const AudioMixerSend &) = default;
};

// Routing settings of one channel. A channel without an entry uses
// AudioMixer::defaultStrip(): tracks and buses feed the master, the master
// feeds "main".
struct AudioMixerStrip {
    QString output;
    AudioLayout layout = AudioLayout::Stereo;
    bool soloSafe = false;
    QVector<AudioMixerSend> sends;
    friend bool operator==(const AudioMixerStrip &, const AudioMixerStrip &) = default;
};

struct AudioMixerBus {
    QString id;            // "bus:<uuid>"
    QString name;
    bool muted = false;
    bool soloed = false;
    friend bool operator==(const AudioMixerBus &, const AudioMixerBus &) = default;
};

// A mute/solo group: its own mute or solo applies to every member (D-112).
struct AudioMixerGroup {
    QString id;            // "group:<uuid>"
    QString name;
    QStringList members;   // track and bus channel IDs
    bool muted = false;
    bool soloed = false;
    friend bool operator==(const AudioMixerGroup &, const AudioMixerGroup &) = default;
};

struct AudioMixerContent {
    QVector<AudioMixerBus> buses;           // mixer order
    QHash<QString, AudioMixerStrip> strips; // only channels that differ from the default
    QVector<AudioMixerGroup> groups;
    bool masterMuted = false;
    AudioPanLaw panLaw = AudioPanLaw::ConstantPower;
    friend bool operator==(const AudioMixerContent &, const AudioMixerContent &) = default;
};

namespace AudioMixer {

inline constexpr int kMaxBuses = 256;
inline constexpr int kMaxSends = 16; // per channel
inline constexpr int kMaxGroups = 64;

QString masterId();   // "master"
QString mainPortId(); // "main"
QString schemaId();   // "qindatk.audio-mixer/1"
QString trackChannelId(const QUuid &trackId);
// The timeline track of "track:<uuid>"; null for any other ID.
QUuid trackOf(const QString &channelId);
bool isTrack(const QString &channelId);
bool isBus(const QString &channelId);
bool isSend(const QString &id);
bool isGroup(const QString &id);
QString newId(const QString &prefix); // "<prefix>:<uuid>"
// "<owner>/<kind>"; kinds: gain, pan, width (channels) and level (sends).
QString parameterId(const QString &owner, const QString &kind);
// The engine parameter behind `kind`: gain and level in dB (range minimum is
// silence, D-101), pan -1..+1 and width 0..2 (steps of 0.01).
std::optional<AutomationParameter> parameterFor(const QString &owner, const QString &kind, const QString &name);

QString layoutName(AudioLayout layout);
std::optional<AudioLayout> layoutFromName(const QString &name);
QStringList layoutNames();
QString panLawName(AudioPanLaw law); // "-3dB", "-4.5dB", "-6dB", "0dB"
std::optional<AudioPanLaw> panLawFromName(const QString &name);
QStringList panLawNames();

// AGENT-CONTRACT (D-111): realtime-safe gain laws. A mono source at `pan`
// (-1 left .. +1 right) feeds a stereo destination with (left, right); the
// centre is symmetric and a hard pan is exactly 0 on the far side.
void panGains(AudioPanLaw law, double pan, double &left, double &right) noexcept;
// A stereo source keeps each side at unity until the balance moves away from it.
void balanceGains(double pan, double &left, double &right) noexcept;

struct Port {
    bool main = false;
    QString deviceId;
    int first = 0; // 1-based device output channel
    int count = 0; // 1 (Mono) or 2 (Stereo)
};
std::optional<Port> parsePort(const QString &id);
QString devicePortId(const QString &deviceId, int first, int count);
bool isPort(const QString &id);

AudioMixerStrip defaultStrip(const QString &channelId);
AudioMixerStrip strip(const AudioMixerContent &content, const QString &channelId);
const AudioMixerBus *bus(const AudioMixerContent &content, const QString &busId);
const AudioMixerGroup *group(const AudioMixerContent &content, const QString &groupId);
// Drops strip entries equal to their default, so equal mixes compare equal.
void normalize(AudioMixerContent &content);
// Channel and bus destinations of `channelId` (its output and sends).
QStringList destinationsOf(const AudioMixerContent &content, const QString &channelId);

// The feedback loop that routing `from` into `to` would close, as channel IDs
// from `to` back to `from`; empty when the route keeps the graph acyclic.
QStringList loopThrough(const AudioMixerContent &content, const QString &from, const QString &to);
// Structural validation (IDs, layouts, destinations, sends, groups and the
// acyclic bus graph). Track channels are not required to exist: a removed
// track keeps its settings for undo. Returns an explanation or "".
QString validate(const AudioMixerContent &content);

// D-112: effective mute and solo over `channels` (tracks, buses, master).
struct Audibility {
    QSet<QString> muted;    // muted, group-muted or implicitly muted by a solo
    QSet<QString> implicit; // silent only because another channel is soloed
    QSet<QString> soloed;   // soloed directly or through a group
    QSet<QString> groupMuted;
    bool anySolo = false;
};
Audibility audibility(const AudioMixerContent &content, const QStringList &channels,
                      const QSet<QString> &trackMuted, const QSet<QString> &trackSoloed);

// "qindatk.audio-mixer/1" maps; an empty map reads as the default mix.
QVariantMap toMap(const AudioMixerContent &content);
std::optional<AudioMixerContent> fromMap(const QVariantMap &map, QString *error);

} // namespace AudioMixer
} // namespace QindaTK
