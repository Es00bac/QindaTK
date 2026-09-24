// SPDX-License-Identifier: LGPL-3.0-or-later
// Compiled only when QINDATK_ENABLE_ALSA is on and libasound links (see the
// top-level CMakeLists.txt); every other file sees createAlsaAudioBackend().
#include "audio_device.h"

#include <QCoreApplication>
#include <alsa/asoundlib.h>
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace QindaTK {
namespace {

QString text(const char *message)
{
    return QCoreApplication::translate("QindaTK::AlsaAudioBackend", message);
}

const QList<int> kRates{44100, 48000, 88200, 96000};
const QList<int> kBuffers{64, 128, 256, 512, 1024, 2048};

struct ConfigDeleter {
    void operator()(snd_config_t *config) const { if (config) snd_config_delete(config); }
};
using ConfigPtr = std::unique_ptr<snd_config_t, ConfigDeleter>;

// AGENT-NOTE: null and file PCMs have no converter clock (writes return at
// once), so the driver paces them by the monotonic clock like the built-in
// devices; hardware and server PCMs pace by blocking I/O.
bool softwareClock(snd_pcm_t *pcm)
{
    const snd_pcm_type_t type = snd_pcm_type(pcm);
    return type == SND_PCM_TYPE_NULL || type == SND_PCM_TYPE_FILE;
}

class AlsaStream final : public AudioDeviceStream {
public:
    ~AlsaStream() override
    {
        stop();
        if (capture) snd_pcm_close(capture);
        if (playback) snd_pcm_close(playback);
    }
    AudioStreamFormat format() const override { return streamFormat; }
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
        if (capture) snd_pcm_drop(capture);
        if (playback) snd_pcm_drop(playback);
    }

    QString configure(snd_pcm_t *pcm, bool input, int channels, int rate, snd_pcm_uframes_t period);
    void allocate()
    {
        const auto frames = size_t(streamFormat.bufferSize);
        in.assign(frames * std::max(1, streamFormat.inputChannels), 0.0f);
        out.assign(frames * std::max(1, streamFormat.outputChannels), 0.0f);
        rawIn.assign(frames * std::max(1, streamFormat.inputChannels) * 4, 0);
        rawOut.assign(frames * std::max(1, streamFormat.outputChannels) * 4, 0);
    }

    snd_pcm_t *capture = nullptr;
    snd_pcm_t *playback = nullptr;
    snd_pcm_format_t captureFormat = SND_PCM_FORMAT_FLOAT_LE;
    snd_pcm_format_t playbackFormat = SND_PCM_FORMAT_FLOAT_LE;
    snd_pcm_uframes_t playbackBuffer = 0;
    AudioStreamFormat streamFormat;
    bool paced = false;

private:
    void run(AudioIoCallback *callback);
    bool transfer(snd_pcm_t *pcm, bool input);

    std::vector<float> in, out;
    std::vector<char> rawIn, rawOut;
    const char *m_failure = "";
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};

QString AlsaStream::configure(snd_pcm_t *pcm, bool input, int channels, int rate, snd_pcm_uframes_t period)
{
    snd_pcm_hw_params_t *hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    if (snd_pcm_hw_params_any(pcm, hw) < 0) return text("The device refused a configuration query");
    snd_pcm_hw_params_set_rate_resample(pcm, hw, 0);
    if (snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
        return text("The device does not support interleaved access");
    snd_pcm_format_t format = SND_PCM_FORMAT_UNKNOWN;
    for (snd_pcm_format_t candidate : {SND_PCM_FORMAT_FLOAT_LE, SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_S16_LE})
        if (snd_pcm_hw_params_test_format(pcm, hw, candidate) == 0) { format = candidate; break; }
    if (format == SND_PCM_FORMAT_UNKNOWN || snd_pcm_hw_params_set_format(pcm, hw, format) < 0)
        return text("The device supports none of float32, s32 or s16 samples");
    if (snd_pcm_hw_params_set_channels(pcm, hw, unsigned(channels)) < 0)
        return text("The device does not provide %1 channel(s)").arg(channels);
    if (snd_pcm_hw_params_set_rate(pcm, hw, unsigned(rate), 0) < 0)
        return text("The device does not support %1 Hz").arg(rate);
    snd_pcm_uframes_t actualPeriod = period;
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &actualPeriod, nullptr);
    snd_pcm_uframes_t buffer = actualPeriod * (input ? 4 : 2);
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
    if (const int error = snd_pcm_hw_params(pcm, hw); error < 0)
        return text("The device rejected the configuration: %1").arg(QString::fromLocal8Bit(snd_strerror(error)));
    snd_pcm_hw_params_get_period_size(hw, &actualPeriod, nullptr);
    snd_pcm_hw_params_get_buffer_size(hw, &buffer);
    snd_pcm_sw_params_t *sw = nullptr;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm, sw);
    snd_pcm_sw_params_set_avail_min(pcm, sw, actualPeriod);
    snd_pcm_sw_params_set_start_threshold(pcm, sw, input ? 1 : buffer);
    snd_pcm_sw_params(pcm, sw);
    if (streamFormat.bufferSize && streamFormat.bufferSize != int(actualPeriod))
        return text("Input and output could not agree on %1 frames per period").arg(streamFormat.bufferSize);
    streamFormat.bufferSize = int(actualPeriod);
    if (input) {
        captureFormat = format;
        streamFormat.inputLatency = int(actualPeriod);
    } else {
        playbackFormat = format;
        playbackBuffer = buffer;
        streamFormat.outputLatency = int(buffer);
    }
    return {};
}

bool AlsaStream::transfer(snd_pcm_t *pcm, bool input)
{
    const int channels = input ? streamFormat.inputChannels : streamFormat.outputChannels;
    const snd_pcm_format_t format = input ? captureFormat : playbackFormat;
    const bool native = format == SND_PCM_FORMAT_FLOAT_LE;
    const int width = format == SND_PCM_FORMAT_S16_LE ? 2 : 4;
    const size_t samples = size_t(streamFormat.bufferSize) * channels;
    char *raw = input ? (native ? reinterpret_cast<char *>(in.data()) : rawIn.data())
                      : (native ? reinterpret_cast<char *>(out.data()) : rawOut.data());
    if (!input && !native)
        for (size_t i = 0; i < samples; ++i) {
            const float v = std::clamp(out[i], -1.0f, 1.0f);
            if (width == 2) reinterpret_cast<qint16 *>(raw)[i] = qint16(std::lrintf(v * 32767.0f));
            else reinterpret_cast<qint32 *>(raw)[i] = qint32(std::llrint(double(v) * 2147483647.0));
        }
    snd_pcm_uframes_t done = 0;
    const auto frames = snd_pcm_uframes_t(streamFormat.bufferSize);
    while (done < frames && !m_stop.load(std::memory_order_acquire)) {
        char *at = raw + done * channels * width;
        const snd_pcm_sframes_t n = input ? snd_pcm_readi(pcm, at, frames - done) : snd_pcm_writei(pcm, at, frames - done);
        if (n > 0) {
            done += snd_pcm_uframes_t(n);
        } else if (n == -EPIPE) {
            m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
            snd_pcm_prepare(pcm);
            if (input) snd_pcm_start(pcm);
        } else if (n == -ESTRPIPE) {
            int resumed;
            while ((resumed = snd_pcm_resume(pcm)) == -EAGAIN) std::this_thread::yield();
            if (resumed < 0) snd_pcm_prepare(pcm);
        } else if (n < 0) {
            m_failure = (n == -ENODEV || n == -EBADFD || n == -EIO || n == -ENXIO)
                        ? "The device was disconnected" : "The device reported an I/O error";
            return false;
        }
    }
    if (input && !native)
        for (size_t i = 0; i < samples; ++i)
            in[i] = width == 2 ? float(reinterpret_cast<const qint16 *>(raw)[i]) / 32768.0f
                               : float(double(reinterpret_cast<const qint32 *>(raw)[i]) / 2147483648.0);
    return true;
}

void AlsaStream::run(AudioIoCallback *callback)
{
    using namespace AudioDeviceThread;
    AudioDeviceThread::begin(m_stats);
    const double periodNs = 1e9 * streamFormat.bufferSize / streamFormat.sampleRate;
    if (playback) {
        std::fill(out.begin(), out.end(), 0.0f);
        for (snd_pcm_uframes_t queued = 0; queued < playbackBuffer; queued += streamFormat.bufferSize)
            if (!transfer(playback, false)) { callback->deviceFailed(m_failure); return; }
    }
    if (capture) snd_pcm_start(capture);
    qint64 origin = monotonicNs();
    quint64 cycle = 0;
    while (!m_stop.load(std::memory_order_acquire)) {
        const qint64 due = origin + qint64(double(cycle) * periodNs);
        if (paced) {
            sleepUntil(due);
            noteWake(m_stats, monotonicNs() - due);
        }
        if (capture && !transfer(capture, true)) { callback->deviceFailed(m_failure); return; }
        const qint64 started = monotonicNs();
        callback->process(in.data(), out.data(), streamFormat.bufferSize);
        const qint64 finished = monotonicNs();
        record(m_stats, started, finished, (paced ? due : started) + qint64(periodNs));
        if (playback && !transfer(playback, false)) { callback->deviceFailed(m_failure); return; }
        if (paced && finished > due + 2 * qint64(periodNs)) {
            m_stats.xruns.fetch_add(1, std::memory_order_relaxed);
            origin = finished;
            cycle = 1;
            continue;
        }
        ++cycle;
    }
}

class AlsaBackend final : public AudioDeviceBackend {
public:
    explicit AlsaBackend(const QByteArray &extra)
    {
        if (extra.isEmpty()) return;
        snd_config_t *top = nullptr;
        snd_input_t *input = nullptr;
        if (snd_config_top(&top) < 0) return;
        ConfigPtr owned(top);
        if (snd_input_buffer_open(&input, extra.constData(), extra.size()) < 0) return;
        const int loaded = snd_config_load(top, input);
        snd_input_close(input);
        if (loaded == 0) m_extra = std::move(owned);
    }
    QString name() const override { return QStringLiteral("alsa"); }
    QList<AudioDeviceInfo> devices() override;
    AudioDeviceStreamPtr open(const AudioDeviceConfig &config, QString *error) override;

private:
    bool definesPcm(const QByteArray &name) const
    {
        snd_config_t *node = nullptr;
        return m_extra && snd_config_search(m_extra.get(), QByteArray("pcm." + name).constData(), &node) == 0;
    }
    AudioDeviceInfo info(const QString &pcm, QString description, const QString &direction) const
    {
        AudioDeviceInfo device;
        device.id = QStringLiteral("alsa:") + pcm;
        device.name = description.replace(QLatin1Char('\n'), QStringLiteral(" · "));
        device.backend = name();
        device.sampleRates = kRates;
        device.bufferSizes = kBuffers;
        device.inputLayouts = {QStringLiteral("None")};
        device.outputLayouts = {QStringLiteral("None")};
        if (direction != QLatin1String("Output")) device.inputLayouts << QStringLiteral("Mono") << QStringLiteral("Stereo");
        if (direction != QLatin1String("Input")) device.outputLayouts << QStringLiteral("Mono") << QStringLiteral("Stereo");
        return device;
    }
    ConfigPtr m_extra;
};

QList<AudioDeviceInfo> AlsaBackend::devices()
{
    // AGENT-NOTE: capability lists are the offered candidates; hw_params
    // negotiation at open decides, and a refusal becomes the acknowledged
    // rejection. Enumeration never opens a device (busy/server PCMs stay idle).
    QList<AudioDeviceInfo> list;
    void **hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) == 0) {
        for (void **hint = hints; *hint; ++hint) {
            char *pcm = snd_device_name_get_hint(*hint, "NAME");
            char *description = snd_device_name_get_hint(*hint, "DESC");
            char *direction = snd_device_name_get_hint(*hint, "IOID");
            if (pcm)
                list.push_back(info(QString::fromLocal8Bit(pcm),
                                    QString::fromLocal8Bit(description ? description : pcm),
                                    QString::fromLatin1(direction ? direction : "")));
            free(pcm);
            free(description);
            free(direction);
        }
        snd_device_name_free_hint(hints);
    }
    snd_config_t *pcms = nullptr;
    if (m_extra && snd_config_search(m_extra.get(), "pcm", &pcms) == 0) {
        snd_config_iterator_t it, next;
        snd_config_for_each(it, next, pcms) {
            const char *id = nullptr;
            if (snd_config_get_id(snd_config_iterator_entry(it), &id) == 0 && id)
                list.push_back(info(QString::fromLocal8Bit(id), text("%1 (configured PCM)").arg(QString::fromLocal8Bit(id)), {}));
        }
    }
    return list;
}

AudioDeviceStreamPtr AlsaBackend::open(const AudioDeviceConfig &config, QString *error)
{
    const QByteArray pcm = config.deviceId.mid(5).toLocal8Bit();
    auto stream = std::make_shared<AlsaStream>();
    stream->streamFormat.sampleRate = config.sampleRate;
    stream->streamFormat.inputChannels = config.inputChannels();
    stream->streamFormat.outputChannels = config.outputChannels();
    const bool local = definesPcm(pcm);
    const auto openPcm = [&](snd_pcm_stream_t direction, snd_pcm_t **handle) -> QString {
        // NONBLOCK open so a busy device fails instead of hanging the worker.
        const int opened = local ? snd_pcm_open_lconf(handle, pcm.constData(), direction, SND_PCM_NONBLOCK, m_extra.get())
                                 : snd_pcm_open(handle, pcm.constData(), direction, SND_PCM_NONBLOCK);
        if (opened < 0) {
            *handle = nullptr;
            return text("%1 cannot be opened: %2").arg(config.deviceId, QString::fromLocal8Bit(snd_strerror(opened)));
        }
        snd_pcm_nonblock(*handle, 0);
        return {};
    };
    QString problem;
    if (config.inputChannels() > 0) {
        problem = openPcm(SND_PCM_STREAM_CAPTURE, &stream->capture);
        if (problem.isEmpty())
            problem = stream->configure(stream->capture, true, config.inputChannels(), config.sampleRate,
                                        snd_pcm_uframes_t(config.bufferSize));
    }
    if (problem.isEmpty() && config.outputChannels() > 0) {
        problem = openPcm(SND_PCM_STREAM_PLAYBACK, &stream->playback);
        if (problem.isEmpty())
            problem = stream->configure(stream->playback, false, config.outputChannels(), config.sampleRate,
                                        snd_pcm_uframes_t(config.bufferSize));
    }
    if (!problem.isEmpty()) {
        if (error) *error = problem;
        return {};
    }
    stream->paced = (!stream->capture || softwareClock(stream->capture))
                    && (!stream->playback || softwareClock(stream->playback));
    stream->streamFormat.hardwareClock = !stream->paced;
    stream->allocate();
    return stream;
}

} // namespace

AudioDeviceBackendPtr createAlsaAudioBackend(const QByteArray &extraConfiguration)
{
    return std::make_shared<AlsaBackend>(extraConfiguration);
}

} // namespace QindaTK
