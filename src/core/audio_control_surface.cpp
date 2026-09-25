// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioControlSurface: the Mackie Control protocol over the mixer's shared
// parameter bindings (docs/audio-mixer.md). The ALSA transport is in
// audio_control_surface_port.cpp.
#include "audio_control_surface.h"

#include "audio_mixer_model.h"
#include "audio_parameter_binding.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {

constexpr quint8 kNoteOn = 0x90;
constexpr quint8 kNoteOff = 0x80;
constexpr quint8 kControl = 0xB0;
constexpr quint8 kPitchBend = 0xE0;
constexpr quint8 kArm = 0x00, kSolo = 0x08, kMute = 0x10, kSelect = 0x18;
constexpr quint8 kBankDown = 0x2E, kBankUp = 0x2F, kChannelDown = 0x30, kChannelUp = 0x31;
constexpr quint8 kTouch = 0x68, kMasterTouch = 0x70;
constexpr quint8 kVpot = 0x10, kRing = 0x30;
constexpr quint8 kOn = 0x7F;

bool accepted(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

} // namespace

void AudioControlSurface::setMixer(AudioMixerModel *mixer)
{
    if (m_mixer == mixer) return;
    releaseAll();
    for (const auto &connection : std::as_const(m_mixerConnections)) disconnect(connection);
    m_mixerConnections.clear();
    m_mixer = mixer;
    m_bank = 0;
    m_pendingBank = -1;
    m_sentFader.clear();
    m_sentRing.clear();
    m_sentLed.clear();
    if (mixer) {
        m_mixerConnections << connect(mixer, &AudioMixerModel::changed, this, [this] {
            // A reorder, a new channel or a removed one moves what the strips show.
            if (m_bank >= shownChannels().size()) setBank(std::max(0, int(shownChannels().size()) - 1));
            rewire();
        });
        m_mixerConnections << connect(mixer, &QObject::destroyed, this, [this] { m_stripConnections.clear(); });
    }
    rewire();
    emit mixerChanged();
    emit bankChanged();
}

QStringList AudioControlSurface::shownChannels() const
{
    if (!m_mixer) return {};
    QStringList ids = m_mixer->strips()->channelIds();
    ids.removeAll(QStringLiteral("master"));
    return ids;
}

QString AudioControlSurface::channelAt(int strip) const
{
    if (!m_mixer) return {};
    if (strip == kMasterStrip) return QStringLiteral("master");
    const QStringList ids = shownChannels();
    const int index = m_bank + strip;
    return strip >= 0 && strip < kStrips && index < ids.size() ? ids.at(index) : QString();
}

QStringList AudioControlSurface::channels() const
{
    QStringList list;
    for (int strip = 0; strip < kStrips; ++strip) list.push_back(channelAt(strip));
    return list;
}

bool AudioControlSurface::held() const
{
    return !m_faders.isEmpty() || !m_vpots.isEmpty();
}

void AudioControlSurface::setBank(int bank)
{
    const int last = std::max(0, int(shownChannels().size()) - 1);
    bank = std::clamp(bank, 0, last);
    // AGENT-GUARD (D-113): the strips never move under a held surface gesture.
    if (held()) {
        m_pendingBank = bank;
        return;
    }
    m_pendingBank = -1;
    if (bank == m_bank) return;
    m_bank = bank;
    rewire();
    emit bankChanged();
}

void AudioControlSurface::applyPendingBank()
{
    if (held() || m_pendingBank < 0) return;
    const int bank = m_pendingBank;
    m_pendingBank = -1;
    setBank(bank);
}

AudioParameterBinding *AudioControlSurface::bindingFor(int strip, const QString &kind)
{
    const QString channel = channelAt(strip);
    return channel.isEmpty() || !m_mixer ? nullptr : m_mixer->parameterBinding(channel, kind);
}

void AudioControlSurface::rewire()
{
    for (const auto &connection : std::as_const(m_stripConnections)) disconnect(connection);
    m_stripConnections.clear();
    for (int strip = 0; strip <= kMasterStrip; ++strip) {
        if (AudioParameterBinding *gain = bindingFor(strip, QStringLiteral("gain")))
            m_stripConnections << connect(gain, &AudioParameterBinding::valueChanged, this, [this, strip] { sendFader(strip); });
        if (strip == kMasterStrip) continue;
        if (AudioParameterBinding *pan = bindingFor(strip, QStringLiteral("pan")))
            m_stripConnections << connect(pan, &AudioParameterBinding::valueChanged, this, [this, strip] { sendRing(strip); });
    }
    for (int strip = 0; strip <= kMasterStrip; ++strip) {
        sendFader(strip);
        if (strip < kStrips) sendRing(strip);
    }
    sendLeds();
}

void AudioControlSurface::refresh()
{
    m_sentFader.clear();
    m_sentRing.clear();
    m_sentLed.clear();
    rewire();
}

bool AudioControlSurface::receive(const QByteArray &bytes)
{
    for (const char c : bytes) {
        const quint8 byte = quint8(c);
        if (byte >= 0xF8) continue; // realtime (clock, active sensing) may interleave anywhere
        if (byte == 0xF0) {
            m_sysex = true;
            continue;
        }
        if (m_sysex) {
            if (byte == 0xF7 || !(byte & 0x80)) {
                if (byte == 0xF7) m_sysex = false;
                continue;
            }
            m_sysex = false; // a status byte ends an unterminated SysEx
        }
        if (byte & 0x80) {
            m_running = byte < 0xF0 ? byte : 0;
            m_pending.clear();
            continue;
        }
        if (!m_running) continue;
        m_pending.append(char(byte));
        const quint8 type = m_running & 0xF0;
        const int need = type == 0xC0 || type == 0xD0 ? 1 : 2;
        if (m_pending.size() < need) continue;
        handle(m_running, quint8(m_pending.at(0)), need > 1 ? quint8(m_pending.at(1)) : 0);
        m_pending.clear();
    }
    return true;
}

void AudioControlSurface::handle(quint8 status, quint8 a, quint8 b)
{
    const quint8 type = status & 0xF0;
    const int channel = status & 0x0F;
    if (type == kPitchBend) {
        if (channel <= kMasterStrip) fader(channel, int(b) << 7 | int(a));
        return;
    }
    if (channel != 0) return;
    if (type == kNoteOn || type == kNoteOff) {
        const bool pressed = type == kNoteOn && b > 0;
        if (a >= kTouch && a < kTouch + kStrips) touch(a - kTouch, pressed);
        else if (a == kMasterTouch) touch(kMasterStrip, pressed);
        else if (pressed) button(a, true);
        return;
    }
    if (type == kControl && a >= kVpot && a < kVpot + kStrips) vpot(a - kVpot, b);
}

void AudioControlSurface::button(quint8 note, bool pressed)
{
    if (!pressed || !m_mixer) return;
    if (note == kBankDown || note == kBankUp) return setBank((m_pendingBank >= 0 ? m_pendingBank : m_bank)
                                                            + (note == kBankUp ? kStrips : -kStrips));
    if (note == kChannelDown || note == kChannelUp) return setBank((m_pendingBank >= 0 ? m_pendingBank : m_bank)
                                                                  + (note == kChannelUp ? 1 : -1));
    const int strip = note & 0x07;
    const QString channel = note < 0x20 ? channelAt(strip) : QString();
    if (channel.isEmpty()) return;
    const QVariantMap row = m_mixer->channel(channel);
    QVariantMap result{{QStringLiteral("ok"), true}};
    if (note < kSolo) result = m_mixer->setArmed(channel, !row.value(QStringLiteral("armed")).toBool());
    else if (note < kMute) result = m_mixer->setSoloed(channel, !row.value(QStringLiteral("soloed")).toBool());
    else if (note < kSelect) result = m_mixer->setMuted(channel, !row.value(QStringLiteral("muted")).toBool());
    else if (m_selected != channel) {
        m_selected = channel;
        emit selectedChannelChanged();
        emit channelSelected(channel);
        sendLeds();
    }
    if (!accepted(result)) setError(result.value(QStringLiteral("error")).toString());
}

AudioControlSurface::Gesture *AudioControlSurface::open(QHash<int, Gesture> &gestures, int strip,
                                                        AudioParameterBinding *binding, bool touched)
{
    if (auto it = gestures.find(strip); it != gestures.end()) {
        it->touched = it->touched || touched;
        return &*it;
    }
    if (!binding) return nullptr;
    Gesture gesture;
    gesture.binding = binding;
    gesture.value = binding->value();
    gesture.silent = binding->silent();
    gesture.touched = touched;
    if (!binding->begin(gesture.value, gesture.silent)) {
        setError(binding->lastError());
        return nullptr;
    }
    const bool first = !held();
    auto it = gestures.insert(strip, gesture);
    if (first) emit heldChanged();
    return &*it;
}

void AudioControlSurface::commit(QHash<int, Gesture> &gestures, int strip)
{
    const auto it = gestures.find(strip);
    if (it == gestures.end()) return;
    const Gesture gesture = *it;
    gestures.erase(it);
    // AGENT-GUARD: the gesture ends on the binding it began on, even when the
    // strip shows another channel by now.
    if (gesture.binding && !gesture.binding->commit(gesture.value, gesture.silent)) setError(gesture.binding->lastError());
    if (!held()) emit heldChanged();
    if (&gestures == &m_faders) sendFader(strip);
    else sendRing(strip);
    applyPendingBank();
}

void AudioControlSurface::touch(int strip, bool on)
{
    if (on) {
        open(m_faders, strip, bindingFor(strip, QStringLiteral("gain")), true);
        return;
    }
    commit(m_faders, strip);
}

void AudioControlSurface::fader(int strip, int travel)
{
    AudioParameterBinding *current = m_faders.contains(strip) ? m_faders.value(strip).binding.data()
                                                              : bindingFor(strip, QStringLiteral("gain"));
    Gesture *gesture = open(m_faders, strip, current, false);
    if (!gesture || !gesture->binding) return;
    const QVariantMap mapped = gesture->binding->mapper()->valueAt(double(std::clamp(travel, 0, kFaderTop)) / kFaderTop);
    gesture->value = mapped.value(QStringLiteral("value")).toDouble();
    gesture->silent = mapped.value(QStringLiteral("silent")).toBool();
    gesture->binding->preview(gesture->value, gesture->silent);
    if (!gesture->touched) m_idle.start();
}

void AudioControlSurface::vpot(int strip, quint8 value)
{
    AudioParameterBinding *current = m_vpots.contains(strip) ? m_vpots.value(strip).binding.data()
                                                             : bindingFor(strip, QStringLiteral("pan"));
    Gesture *gesture = open(m_vpots, strip, current, false);
    if (!gesture || !gesture->binding) return;
    const double step = gesture->binding->step() > 0 ? gesture->binding->step() : 0.01;
    const int steps = (value & 0x40) ? -int(value & 0x3F) : int(value & 0x3F);
    gesture->value = gesture->binding->canonical(gesture->value + steps * step);
    gesture->binding->preview(gesture->value, false);
    m_idle.start();
}

void AudioControlSurface::idle()
{
    // A burst from a surface without touch sensing (or a V-Pot) is one step.
    for (const int strip : m_faders.keys())
        if (!m_faders.value(strip).touched) commit(m_faders, strip);
    for (const int strip : m_vpots.keys()) commit(m_vpots, strip);
}

void AudioControlSurface::releaseAll()
{
    m_idle.stop();
    for (const int strip : m_faders.keys()) commit(m_faders, strip);
    for (const int strip : m_vpots.keys()) commit(m_vpots, strip);
}

void AudioControlSurface::sendFader(int strip)
{
    // A fader under the user's hand is never driven against it.
    if (m_faders.contains(strip)) return;
    AudioParameterBinding *gain = bindingFor(strip, QStringLiteral("gain"));
    const int travel = gain ? int(std::lround(gain->mapper()->positionFor(gain->value(), gain->silent()) * kFaderTop)) : 0;
    if (m_sentFader.value(strip, -1) == travel) return;
    m_sentFader.insert(strip, travel);
    send(quint8(kPitchBend | strip), quint8(travel & 0x7F), quint8(travel >> 7));
}

void AudioControlSurface::sendRing(int strip)
{
    if (strip < 0 || strip >= kStrips || m_vpots.contains(strip)) return;
    AudioParameterBinding *pan = bindingFor(strip, QStringLiteral("pan"));
    // Dot mode: LED 1 (left) .. 6 (centre) .. 11 (right); 0 is dark.
    const int ring = pan ? 1 + int(std::lround((std::clamp(pan->value(), -1.0, 1.0) + 1.0) * 5.0)) : 0;
    if (m_sentRing.value(strip, -1) == ring) return;
    m_sentRing.insert(strip, ring);
    send(kControl, quint8(kRing + strip), quint8(ring));
}

void AudioControlSurface::sendLeds()
{
    for (int strip = 0; strip < kStrips; ++strip) {
        const QString channel = channelAt(strip);
        const QVariantMap row = channel.isEmpty() || !m_mixer ? QVariantMap() : m_mixer->channel(channel);
        const QList<QPair<quint8, bool>> leds{{quint8(kArm + strip), row.value(QStringLiteral("armed")).toBool()},
                                              {quint8(kSolo + strip), row.value(QStringLiteral("soloed")).toBool()},
                                              {quint8(kMute + strip), row.value(QStringLiteral("muted")).toBool()},
                                              {quint8(kSelect + strip), !channel.isEmpty() && channel == m_selected}};
        for (const auto &[note, on] : leds) {
            const int velocity = on ? kOn : 0;
            if (m_sentLed.value(note, -1) == velocity) continue;
            m_sentLed.insert(note, velocity);
            send(kNoteOn, note, quint8(velocity));
        }
    }
}

void AudioControlSurface::send(quint8 status, quint8 a, quint8 b)
{
    QByteArray message;
    message.append(char(status)).append(char(a)).append(char(b));
    emit outgoing(message);
    portWrite(message);
}

void AudioControlSurface::setError(const QString &message)
{
    if (message.isEmpty() || message == m_lastError) return;
    m_lastError = message;
    emit lastErrorChanged();
}

} // namespace QindaTK
