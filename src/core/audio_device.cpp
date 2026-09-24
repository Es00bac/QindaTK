// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_device.h"

#include <QCoreApplication>
#include <QVariantList>
#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>
#if defined(Q_OS_UNIX)
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <cerrno>
#endif

namespace QindaTK {
namespace {

QString text(const char *message)
{
    return QCoreApplication::translate("QindaTK::AudioDevice", message);
}

QVariantList variants(const QList<int> &values)
{
    QVariantList out;
    for (int value : values) out.push_back(value);
    return out;
}

const QList<int> kBuiltinRates{44100, 48000, 88200, 96000};
const QList<int> kBuiltinBuffers{64, 128, 256, 512, 1024};
const QStringList kLayouts{QStringLiteral("None"), QStringLiteral("Mono"), QStringLiteral("Stereo")};

} // namespace

QVariantMap AudioDeviceInfo::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("backend"), backend},
            {QStringLiteral("available"), available},
            {QStringLiteral("unavailableReason"), unavailableReason},
            {QStringLiteral("sampleRates"), variants(sampleRates)},
            {QStringLiteral("bufferSizes"), variants(bufferSizes)},
            {QStringLiteral("inputLayouts"), inputLayouts},
            {QStringLiteral("outputLayouts"), outputLayouts}};
}

int AudioDeviceConfig::channelsFor(const QString &layout)
{
    if (layout == QLatin1String("None")) return 0;
    if (layout == QLatin1String("Mono")) return 1;
    if (layout == QLatin1String("Stereo")) return 2;
    return -1;
}

QVariantMap AudioDeviceConfig::toMap() const
{
    return {{QStringLiteral("deviceId"), deviceId},
            {QStringLiteral("sampleRate"), sampleRate},
            {QStringLiteral("bufferSize"), bufferSize},
            {QStringLiteral("inputLayout"), inputLayout},
            {QStringLiteral("outputLayout"), outputLayout}};
}

std::optional<AudioDeviceConfig> AudioDeviceConfig::fromMap(const QVariantMap &map, QString *error)
{
    AudioDeviceConfig config;
    config.deviceId = map.value(QStringLiteral("deviceId")).toString();
    bool rateOk = false, bufferOk = false;
    config.sampleRate = map.value(QStringLiteral("sampleRate")).toInt(&rateOk);
    config.bufferSize = map.value(QStringLiteral("bufferSize")).toInt(&bufferOk);
    config.inputLayout = map.value(QStringLiteral("inputLayout"), QStringLiteral("None")).toString();
    config.outputLayout = map.value(QStringLiteral("outputLayout"), QStringLiteral("Stereo")).toString();
    QString problem;
    if (!config.deviceId.contains(QLatin1Char(':')))
        problem = text("A configuration needs a stable device ID");
    else if (!rateOk || config.sampleRate < 8000 || config.sampleRate > 384000)
        problem = text("The sample rate must be between 8000 and 384000 Hz");
    else if (!bufferOk || config.bufferSize < 16 || config.bufferSize > 8192)
        problem = text("The buffer size must be between 16 and 8192 frames");
    else if (config.inputChannels() < 0 || config.outputChannels() < 0)
        problem = text("Supported layouts are None, Mono and Stereo");
    else if (config.inputChannels() + config.outputChannels() == 0)
        problem = text("A configuration needs an input or an output");
    if (problem.isEmpty()) return config;
    if (error) *error = problem;
    return std::nullopt;
}

// ---- device-thread helpers -------------------------------------------------

qint64 AudioDeviceThread::monotonicNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AudioDeviceThread::sleepUntil(qint64 monotonicNs)
{
#if defined(Q_OS_LINUX)
    const timespec until{time_t(monotonicNs / 1000000000), long(monotonicNs % 1000000000)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr) == EINTR) {}
#else
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(monotonicNs)));
#endif
}

void AudioDeviceThread::begin(AudioCycleStats &stats)
{
#if defined(Q_OS_UNIX)
    stats.threadToken.store(quint64(pthread_self()), std::memory_order_release);
    sched_param parameter{};
    parameter.sched_priority = std::min(70, sched_get_priority_max(SCHED_FIFO));
    stats.realtimePriority.store(pthread_setschedparam(pthread_self(), SCHED_FIFO, &parameter) == 0);
#else
    stats.threadToken.store(quint64(std::hash<std::thread::id>{}(std::this_thread::get_id())));
#endif
}

void AudioDeviceThread::noteWake(AudioCycleStats &stats, qint64 latenessNs)
{
    if (latenessNs > stats.maxWakeLatenessNs.load(std::memory_order_relaxed))
        stats.maxWakeLatenessNs.store(latenessNs, std::memory_order_relaxed);
}

void AudioDeviceThread::record(AudioCycleStats &stats, qint64 startedNs, qint64 finishedNs, qint64 deadlineNs)
{
    const qint64 spent = finishedNs - startedNs;
    stats.totalCallbackNs.fetch_add(spent, std::memory_order_relaxed);
    if (spent > stats.maxCallbackNs.load(std::memory_order_relaxed))
        stats.maxCallbackNs.store(spent, std::memory_order_relaxed);
    if (finishedNs > deadlineNs) stats.lateCallbacks.fetch_add(1, std::memory_order_relaxed);
    stats.callbacks.fetch_add(1, std::memory_order_release);
}

// ---- built-in reference devices -------------------------------------------

struct BuiltinAudioBackend::State {
    std::atomic<bool> nullConnected{true};
    std::atomic<bool> loopbackConnected{true};
    std::atomic<bool> *flag(const QString &id)
    {
        if (id == QLatin1String("builtin:null")) return &nullConnected;
        if (id == QLatin1String("builtin:loopback")) return &loopbackConnected;
        return nullptr;
    }
};

namespace {

class BuiltinStream final : public AudioDeviceStream {
public:
    BuiltinStream(std::shared_ptr<BuiltinAudioBackend::State> state, std::atomic<bool> *connected,
                  bool loopback, const AudioStreamFormat &format)
        : m_state(std::move(state)), m_connected(connected), m_loopback(loopback), m_format(format),
          m_input(size_t(format.bufferSize) * std::max(1, format.inputChannels), 0.0f),
          m_output(size_t(format.bufferSize) * std::max(1, format.outputChannels), 0.0f) {}
    ~BuiltinStream() override { stop(); }
    AudioStreamFormat format() const override { return m_format; }
    bool start(AudioIoCallback *callback, QString *error) override
    {
        if (m_thread.joinable()) {
            if (error) *error = text("The stream is already running");
            return false;
        }
        m_stop.store(false);
        m_thread = std::thread([this, callback] { run(callback); });
        return true;
    }
    void stop() override
    {
        m_stop.store(true, std::memory_order_release);
        if (m_thread.joinable()) m_thread.join();
    }

private:
    void run(AudioIoCallback *callback)
    {
        using namespace AudioDeviceThread;
        AudioDeviceThread::begin(m_stats);
        const int frames = m_format.bufferSize;
        const int in = m_format.inputChannels, out = m_format.outputChannels;
        const double periodNs = 1e9 * frames / m_format.sampleRate;
        qint64 origin = monotonicNs();
        quint64 cycle = 0;
        while (!m_stop.load(std::memory_order_acquire)) {
            const qint64 due = origin + qint64(double(cycle) * periodNs);
            sleepUntil(due);
            const qint64 woke = monotonicNs();
            noteWake(m_stats, woke - due);
            if (!m_connected->load(std::memory_order_acquire)) {
                callback->deviceFailed("The device was disconnected");
                return;
            }
            // m_output still holds the previous period: that is the loopback input.
            if (m_loopback && in > 0)
                for (int i = 0; i < frames; ++i)
                    for (int c = 0; c < in; ++c)
                        m_input[size_t(i * in + c)] = out > 0 ? m_output[size_t(i * out + std::min(c, out - 1))] : 0.0f;
            callback->process(m_input.data(), m_output.data(), frames);
            const qint64 finished = monotonicNs();
            record(m_stats, woke, finished, due + qint64(periodNs));
            if (finished > due + 2 * qint64(periodNs)) {
                // A converter would have run dry a whole period ago: count an
                // xrun and re-anchor instead of bursting to catch up.
                m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
                origin = finished;
                cycle = 1;
                continue;
            }
            ++cycle;
        }
    }

    std::shared_ptr<BuiltinAudioBackend::State> m_state;
    std::atomic<bool> *m_connected;
    const bool m_loopback;
    const AudioStreamFormat m_format;
    std::vector<float> m_input;
    std::vector<float> m_output;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};

} // namespace

BuiltinAudioBackend::BuiltinAudioBackend()
    : m_state(std::make_shared<State>()) {}

QList<AudioDeviceInfo> BuiltinAudioBackend::devices()
{
    QList<AudioDeviceInfo> list;
    const auto add = [&](const QString &id, const QString &name) {
        AudioDeviceInfo info;
        info.id = id;
        info.name = name;
        info.backend = this->name();
        info.available = m_state->flag(id)->load();
        info.unavailableReason = info.available ? QString() : text("Disconnected");
        info.sampleRates = kBuiltinRates;
        info.bufferSizes = kBuiltinBuffers;
        info.inputLayouts = kLayouts;
        info.outputLayouts = kLayouts;
        list.push_back(info);
    };
    add(QStringLiteral("builtin:null"), text("Null device (silent)"));
    add(QStringLiteral("builtin:loopback"), text("Loopback (output to input, silent)"));
    return list;
}

AudioDeviceStreamPtr BuiltinAudioBackend::open(const AudioDeviceConfig &config, QString *error)
{
    std::atomic<bool> *connected = m_state->flag(config.deviceId);
    QString problem;
    if (!connected) problem = text("Unknown built-in device %1").arg(config.deviceId);
    else if (!connected->load()) problem = text("The device %1 is disconnected").arg(config.deviceId);
    if (!problem.isEmpty()) {
        if (error) *error = problem;
        return {};
    }
    AudioStreamFormat format;
    format.sampleRate = config.sampleRate;
    format.bufferSize = config.bufferSize;
    format.inputChannels = config.inputChannels();
    format.outputChannels = config.outputChannels();
    const bool loopback = config.deviceId == QLatin1String("builtin:loopback");
    format.outputLatency = loopback ? config.bufferSize : 0;
    format.hardwareClock = false;
    return std::make_shared<BuiltinStream>(m_state, connected, loopback, format);
}

bool BuiltinAudioBackend::setConnected(const QString &deviceId, bool connected)
{
    std::atomic<bool> *flag = m_state->flag(deviceId);
    if (!flag) return false;
    flag->store(connected, std::memory_order_release);
    return true;
}

#if !defined(QINDATK_HAVE_ALSA)
AudioDeviceBackendPtr createAlsaAudioBackend(const QByteArray &)
{
    return {};
}
#endif

} // namespace QindaTK
