// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <optional>

namespace QindaTK {

// One enumerated device. `id` is "<backend>:<name>": stable across
// enumeration order, never an index (PLT-19).
struct AudioDeviceInfo {
    QString id;
    QString name;
    QString backend;
    bool available = true;
    QString unavailableReason;
    QList<int> sampleRates;
    QList<int> bufferSizes;
    QStringList inputLayouts;   // "None", "Mono", "Stereo"
    QStringList outputLayouts;
    QVariantMap toMap() const;  // the DeviceSettingsPanel device record
};

struct AudioDeviceConfig {
    QString deviceId;
    int sampleRate = 48000;
    int bufferSize = 256;
    QString inputLayout = QStringLiteral("None");
    QString outputLayout = QStringLiteral("Stereo");
    static int channelsFor(const QString &layout); // None 0, Mono 1, Stereo 2, unknown -1
    int inputChannels() const { return channelsFor(inputLayout); }
    int outputChannels() const { return channelsFor(outputLayout); }
    QVariantMap toMap() const;
    static std::optional<AudioDeviceConfig> fromMap(const QVariantMap &map, QString *error);
    friend bool operator==(const AudioDeviceConfig &, const AudioDeviceConfig &) = default;
};

// What an open stream actually runs at. Latencies are nominal frame counts
// from the driver's buffer configuration (docs/audio-engine.md).
struct AudioStreamFormat {
    int sampleRate = 0;
    int bufferSize = 0;          // frames per callback
    int inputChannels = 0;
    int outputChannels = 0;
    int inputLatency = 0;        // frames between the converter and the callback
    int outputLatency = 0;       // frames between the callback and the converter
    bool hardwareClock = false;  // false: the driver paces by the monotonic clock
};

// Written only by the device thread; readable from any thread.
struct AudioCycleStats {
    std::atomic<quint64> callbacks{0};
    std::atomic<quint64> xruns{0};          // device-reported (or a whole paced period missed)
    std::atomic<quint64> lateCallbacks{0};  // finished after the period deadline
    std::atomic<qint64> maxCallbackNs{0};
    std::atomic<qint64> totalCallbackNs{0};
    std::atomic<qint64> maxWakeLatenessNs{0};
    std::atomic<quint64> threadToken{0};    // native handle of the device thread
    std::atomic<bool> realtimePriority{false};
};

class AudioIoCallback {
public:
    virtual ~AudioIoCallback() = default;
    // AGENT-CONTRACT: called on the device thread once per period with
    // interleaved float32 buffers (frames * channels) owned by the driver.
    // Implementations never allocate, free, lock, block, do I/O or call Qt.
    virtual void process(const float *input, float *output, int frames) noexcept = 0;
    // The device failed (unplugged, I/O error); the device thread exits right
    // after this call. `reason` is static text.
    virtual void deviceFailed(const char *reason) noexcept = 0;
};

class AudioDeviceStream {
public:
    virtual ~AudioDeviceStream() = default;
    virtual AudioStreamFormat format() const = 0;
    virtual bool start(AudioIoCallback *callback, QString *error) = 0;
    virtual void stop() = 0; // joins the device thread; idempotent
    const AudioCycleStats &stats() const { return m_stats; }
protected:
    AudioCycleStats m_stats;
};
using AudioDeviceStreamPtr = std::shared_ptr<AudioDeviceStream>;

class AudioDeviceBackend {
public:
    virtual ~AudioDeviceBackend() = default;
    virtual QString name() const = 0; // device ID prefix
    virtual QList<AudioDeviceInfo> devices() = 0;
    // Opens and configures without starting. Fails rather than substituting a
    // different rate or channel count.
    virtual AudioDeviceStreamPtr open(const AudioDeviceConfig &config, QString *error) = 0;
};
using AudioDeviceBackendPtr = std::shared_ptr<AudioDeviceBackend>;

// Software reference devices, always built and always silent:
//   builtin:null      discards output and captures silence;
//   builtin:loopback  captures the previous period's output (round trip one period).
// Each runs a real device thread paced by the monotonic clock, so callback
// deadlines are measured against wall time. setConnected() simulates unplug.
class BuiltinAudioBackend final : public AudioDeviceBackend {
public:
    BuiltinAudioBackend();
    QString name() const override { return QStringLiteral("builtin"); }
    QList<AudioDeviceInfo> devices() override;
    AudioDeviceStreamPtr open(const AudioDeviceConfig &config, QString *error) override;
    bool setConnected(const QString &deviceId, bool connected);
    struct State;
private:
    std::shared_ptr<State> m_state;
};

// ALSA PCMs as "alsa:<pcm name>" when built with QINDATK_ENABLE_ALSA and
// libasound links; nullptr otherwise. `extraConfiguration` is ALSA config text
// whose pcm.<name> definitions are listed and opened too (silent fixtures).
AudioDeviceBackendPtr createAlsaAudioBackend(const QByteArray &extraConfiguration = {});

// Shared device-thread helpers (built-in and ALSA drivers).
namespace AudioDeviceThread {
qint64 monotonicNs();
void sleepUntil(qint64 monotonicNs);
void begin(AudioCycleStats &stats); // thread token + SCHED_FIFO when permitted
void noteWake(AudioCycleStats &stats, qint64 latenessNs);
void record(AudioCycleStats &stats, qint64 startedNs, qint64 finishedNs, qint64 deadlineNs);
} // namespace AudioDeviceThread

} // namespace QindaTK
