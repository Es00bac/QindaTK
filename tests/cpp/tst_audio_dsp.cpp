// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-063 signal fixtures: FFT and window conventions (D-136), known tones and
// impulses at exact bins, spectral processing boundaries and reconstruction
// (D-137), exact peak-pyramid extrema without dropped peaks (D-135), tile
// seams, strided bounds and cancellation.
#include "audio_dsp.h"
#include "audio_peaks.h"
#include "audio_spectrogram.h"

#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;
constexpr double Pi = AudioDsp::kPi;

std::shared_ptr<AudioBuffer> makeBuffer(qint64 frames, int channels = 1)
{
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->sampleRate = Rate;
    buffer->channels = channels;
    buffer->frames = frames;
    buffer->samples.assign(size_t(frames) * size_t(channels), 0.0f);
    return buffer;
}

// Sum of sines at exact frequencies, amplitude per tone.
std::shared_ptr<AudioBuffer> tones(qint64 frames, const QList<QPair<double, double>> &parts)
{
    auto buffer = makeBuffer(frames);
    for (qint64 n = 0; n < frames; ++n) {
        double value = 0.0;
        for (const auto &part : parts) value += part.second * std::sin(2.0 * Pi * part.first * double(n) / Rate);
        buffer->samples[size_t(n)] = float(value);
    }
    return buffer;
}

// Deterministic pseudo-random values in [-1, 1].
struct Lcg {
    quint32 state = 12345;
    quint32 next() { return state = state * 1664525u + 1013904223u; }
    float value() { return float(double(next() >> 8) / double(1 << 24) * 2.0 - 1.0); }
};

std::vector<float> magnitudes(const AudioBuffer &buffer, int fftSize, qint64 centre)
{
    AudioDsp::FrameAnalyzer analyzer(fftSize);
    std::vector<float> out(size_t(fftSize / 2 + 1));
    analyzer.magnitudes(buffer.channel(0), buffer.frames, centre, out.data());
    return out;
}

} // namespace

class TestAudioDsp : public QObject {
    Q_OBJECT
private slots:
    void fftMatchesTheDefinition()
    {
        constexpr int N = 64;
        Lcg random;
        std::vector<std::complex<double>> input(N), data(N);
        for (int i = 0; i < N; ++i) input[size_t(i)] = {random.value(), random.value()};
        data = input;
        const AudioDsp::Fft fft(N);
        fft.forward(data.data());
        for (int k = 0; k < N; ++k) {
            std::complex<double> expected;
            for (int n = 0; n < N; ++n) expected += input[size_t(n)] * std::polar(1.0, -2.0 * Pi * k * n / N);
            QVERIFY(std::abs(data[size_t(k)] - expected) < 1e-9);
        }
        fft.inverse(data.data());
        for (int n = 0; n < N; ++n) QVERIFY(std::abs(data[size_t(n)] - input[size_t(n)]) < 1e-12);
    }

    void hannWindowConventions()
    {
        const std::vector<double> window = AudioDsp::hannWindow(2048);
        double sum = 0.0;
        for (double w : window) sum += w;
        QVERIFY(std::fabs(sum - 1024.0) < 1e-9); // Σw = N/2
        QCOMPARE(window[0], 0.0);
        QVERIFY(std::fabs(window[1024] - 1.0) < 1e-15); // the centre sample
        QCOMPARE(AudioDsp::binFrequency(64, 2048, Rate), 1500.0);
        QVERIFY(AudioDsp::validFftSize(64) && AudioDsp::validFftSize(16384));
        QVERIFY(!AudioDsp::validFftSize(32) && !AudioDsp::validFftSize(3000) && !AudioDsp::validFftSize(32768));
        QCOMPARE(AudioDsp::gainFromDecibels(AudioDsp::kSilenceDb), 0.0);
    }

    void fullScaleToneReadsZeroDbfsOnItsBin()
    {
        // 1500 Hz at 48 kHz with N = 2048 is exactly bin 64.
        const auto buffer = tones(48000, {{1500.0, 1.0}});
        const std::vector<float> m = magnitudes(*buffer, 2048, 24000);
        QVERIFY(std::fabs(m[64] - 1.0f) < 1e-6f);
        QVERIFY(std::fabs(m[63] - 0.5f) < 1e-6f); // Hann leakage: half amplitude on each neighbour
        QVERIFY(std::fabs(m[65] - 0.5f) < 1e-6f);
        QVERIFY(m[62] < 1e-6f && m[66] < 1e-6f && m[0] < 1e-6f && m[400] < 1e-6f);
        QVERIFY(std::fabs(AudioDsp::amplitudeToDecibels(m[64])) < 1e-4f);
        QVERIFY(std::fabs(AudioDsp::amplitudeToDecibels(m[63]) + 6.0206f) < 1e-3f);
    }

    void impulseIsFlatAtTheWindowGain()
    {
        auto buffer = makeBuffer(8192);
        buffer->samples[4096] = 1.0f;
        const std::vector<float> m = magnitudes(*buffer, 2048, 4096);
        QVERIFY(std::fabs(m[0] - 2.0f / 2048) < 1e-9f);    // DC and Nyquist: 1/Σw
        QVERIFY(std::fabs(m[1024] - 2.0f / 2048) < 1e-9f);
        for (int k : {1, 17, 300, 1023}) QVERIFY(std::fabs(m[size_t(k)] - 4.0f / 2048) < 1e-9f); // 2/Σw
        // Zero padding: a frame centred before the start reads only real samples.
        const std::vector<float> early = magnitudes(*buffer, 2048, 0);
        QCOMPARE(early[5], 0.0f);
    }

    void spectralRemovalTakesOnlyTheBand()
    {
        const auto buffer = tones(48000, {{1500.0, 0.5}, {6000.0, 0.5}});
        AudioDsp::SpectralGainSpec spec{0, 48000, 5000.0, 7000.0, 0.0, 2048, Rate};
        const auto patch = AudioDsp::spectralGain(buffer->channel(0), buffer->frames, spec);
        QVERIFY(patch);
        QCOMPARE(patch->start, qint64(0));
        QCOMPARE(qint64(patch->samples.size()), qint64(48000));
        auto processed = makeBuffer(48000);
        processed->samples = patch->samples;
        const std::vector<float> m = magnitudes(*processed, 2048, 24000);
        QVERIFY(m[256] < 1e-5f); // 6 kHz: removed (below -100 dB)
        QVERIFY(std::fabs(m[64] - 0.5f) < 1e-5f); // 1.5 kHz: kept
    }

    void spectralPatchBoundariesAreExact()
    {
        const auto buffer = tones(48000, {{1500.0, 0.5}, {6000.0, 0.5}});
        // Frames centred at 12288 … 35840 (hop 512) are processed.
        AudioDsp::SpectralGainSpec spec{12000, 36000, 5000.0, 7000.0, 0.0, 2048, Rate};
        const auto range = AudioDsp::spectralPatchRange(spec, buffer->frames);
        QVERIFY(range);
        QCOMPARE(range->first, qint64(12288 - 1024));
        QCOMPARE(range->second, qint64(35840 + 1024));
        const auto patch = AudioDsp::spectralGain(buffer->channel(0), buffer->frames, spec);
        QVERIFY(patch);
        QCOMPARE(patch->start, range->first);
        QCOMPARE(qint64(patch->samples.size()), range->second - range->first);
        // A band holding no energy reconstructs the samples to rounding.
        AudioDsp::SpectralGainSpec empty{12000, 36000, 20000.0, 21000.0, 0.0, 2048, Rate};
        const auto same = AudioDsp::spectralGain(buffer->channel(0), buffer->frames, empty);
        QVERIFY(same);
        for (size_t i = 0; i < same->samples.size(); ++i)
            QVERIFY(std::fabs(same->samples[i] - buffer->samples[size_t(same->start) + i]) < 1e-6f);
        // Shorter than a hop between frame centres: nothing to process.
        QVERIFY(!AudioDsp::spectralPatchRange({100, 300, 0.0, 1000.0, 0.0, 2048, Rate}, buffer->frames));
    }

    void maskBinRangesFollowThePolygon()
    {
        // Triangle (0, 1 kHz) – (1000, 1 kHz) – (0, 9 kHz): at sample 500 the
        // inside is 1 kHz … 5 kHz; bins are 23.4375 Hz apart at N = 2048.
        const std::vector<AudioDsp::SpectralMaskPolygon> triangle{{{0, 1000.0}, {1000, 1000.0}, {0, 9000.0}}};
        auto ranges = AudioDsp::maskBinRanges(triangle, 500.0, 2048, Rate);
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].first, 43);   // ceil(1000 / 23.4375)
        QCOMPARE(ranges[0].second, 213); // floor(5000 / 23.4375)
        QVERIFY(AudioDsp::maskBinRanges(triangle, 1500.0, 2048, Rate).empty()); // past the polygon
        // Paint dabs are a union: two polygons give both ranges.
        std::vector<AudioDsp::SpectralMaskPolygon> dabs = triangle;
        dabs.push_back({{400, 12000.0}, {600, 12000.0}, {600, 14000.0}, {400, 14000.0}});
        ranges = AudioDsp::maskBinRanges(dabs, 500.0, 2048, Rate);
        QCOMPARE(int(ranges.size()), 2);
        QCOMPARE(ranges[1].first, 512);  // 12000 Hz is bin 512 exactly
        QCOMPARE(ranges[1].second, 597); // floor(14000 / 23.4375)
    }

    void maskedGainMatchesTheBandAndStaysInsideTheMask()
    {
        const auto buffer = tones(48000, {{1500.0, 0.5}, {6000.0, 0.5}});
        // A rectangle mask is the band edit, bit for bit.
        AudioDsp::SpectralGainSpec band{12000, 36000, 5000.0, 7000.0, 0.0, 2048, Rate, {}};
        AudioDsp::SpectralGainSpec masked = band;
        masked.mask = {{{12000, 5000.0}, {36000, 5000.0}, {36000, 7000.0}, {12000, 7000.0}}};
        const auto a = AudioDsp::spectralGain(buffer->channel(0), buffer->frames, band);
        const auto b = AudioDsp::spectralGain(buffer->channel(0), buffer->frames, masked);
        QVERIFY(a && b);
        QCOMPARE(b->start, a->start);
        QVERIFY(b->samples == a->samples);
        // A mask covering only the second half of the range leaves the first
        // half's frames exactly as they were and removes the tone after it.
        masked.mask = {{{24000, 5000.0}, {36000, 5000.0}, {36000, 7000.0}, {24000, 7000.0}}};
        const auto half = AudioDsp::spectralGain(buffer->channel(0), buffer->frames, masked);
        QVERIFY(half);
        auto processed = makeBuffer(48000);
        processed->samples = buffer->samples;
        std::copy(half->samples.begin(), half->samples.end(), processed->samples.begin() + half->start);
        for (qint64 i = 12000; i < 24000 - 1024; ++i) QCOMPARE(processed->samples[size_t(i)], buffer->samples[size_t(i)]);
        QVERIFY(magnitudes(*processed, 2048, 30720)[256] < 1e-5f);                 // 6 kHz removed inside
        QVERIFY(std::fabs(magnitudes(*processed, 2048, 30720)[64] - 0.5f) < 1e-5f); // 1.5 kHz kept
        QVERIFY(std::fabs(magnitudes(*processed, 2048, 16384)[256] - 0.5f) < 1e-5f); // 6 kHz kept before
    }

    void spectralProcessingCancels()
    {
        const auto buffer = tones(48000, {{1500.0, 0.5}});
        CancellationSource cancel;
        cancel.cancel();
        const AudioDsp::SpectralGainSpec spec{0, 48000, 1000.0, 2000.0, 0.0, 2048, Rate};
        QVERIFY(!AudioDsp::spectralGain(buffer->channel(0), buffer->frames, spec, cancel.token()));
    }

    void pyramidExtremaAreExact()
    {
        auto buffer = makeBuffer(100003, 2);
        Lcg random;
        for (float &sample : buffer->samples) sample = random.value();
        const AudioPeakPyramidPtr pyramid = AudioPeakPyramid::build(buffer);
        QVERIFY(pyramid);
        QVERIFY(pyramid->levels() > 3);
        QVERIFY(pyramid->memoryBytes() < buffer->frames * buffer->channels); // under one byte per sample
        for (int trial = 0; trial < 300; ++trial) {
            qint64 a = qint64(random.next() % quint32(buffer->frames));
            qint64 b = qint64(random.next() % quint32(buffer->frames + 1));
            if (a > b) std::swap(a, b);
            if (a == b) ++b;
            const int channel = int(random.next() % 3) - 1;
            float lo = 0.0f, hi = 0.0f;
            bool any = false;
            for (int c = channel < 0 ? 0 : channel; c < (channel < 0 ? 2 : channel + 1); ++c)
                for (qint64 i = a; i < b; ++i) {
                    const float v = buffer->channel(c)[i];
                    lo = any ? std::min(lo, v) : v;
                    hi = any ? std::max(hi, v) : v;
                    any = true;
                }
            const AudioPeakPyramid::Extrema e = pyramid->extrema(channel, a, b);
            QCOMPARE(e.minimum, lo);
            QCOMPARE(e.maximum, hi);
        }
    }

    void pyramidNeverDropsAPeak()
    {
        auto buffer = makeBuffer(200000);
        buffer->samples[65537] = 0.9f;  // just past a level boundary (65536 = 16·4^6)
        buffer->samples[777] = -0.7f;
        buffer->samples[199999] = 0.25f; // the last sample, in a partial block
        const AudioPeakPyramidPtr pyramid = AudioPeakPyramid::build(buffer);
        QVERIFY(pyramid);
        for (int count : {1, 3, 7, 100, 1000, 4096, 8192}) {
            const auto columns = pyramid->columns(0, 0, buffer->frames, count);
            QCOMPARE(int(columns.size()), count);
            int highs = 0, lows = 0, tail = 0;
            for (const auto &column : columns) {
                if (column.maximum == 0.9f) {
                    ++highs;
                    QVERIFY(column.start <= 65537 && 65537 < column.end);
                }
                if (column.minimum == -0.7f) {
                    ++lows;
                    QVERIFY(column.start <= 777 && 777 < column.end);
                }
                if (column.maximum == 0.25f || (column.end == 200000 && column.maximum >= 0.25f)) ++tail;
            }
            QCOMPARE(highs, 1);
            QCOMPARE(lows, 1);
            QCOMPARE(tail, 1);
            QCOMPARE(columns.front().start, qint64(0));
            QCOMPARE(columns.back().end, qint64(200000));
        }
    }

    void columnsHaveExactBoundariesAndReachSamples()
    {
        auto buffer = makeBuffer(1000);
        for (qint64 i = 0; i < 1000; ++i) buffer->samples[size_t(i)] = float(i) / 1024.0f;
        const AudioPeakPyramidPtr pyramid = AudioPeakPyramid::build(buffer);
        const auto thirds = pyramid->columns(0, 0, 1000, 3);
        QCOMPARE(int(thirds.size()), 3);
        QCOMPARE(thirds[0].end, qint64(333));
        QCOMPARE(thirds[1].end, qint64(666));
        QCOMPARE(thirds[2].end, qint64(1000));
        QCOMPARE(thirds[1].minimum, 333.0f / 1024.0f);
        QCOMPARE(thirds[1].maximum, 665.0f / 1024.0f);
        // Sample zoom: ten samples in 400 requested columns are ten one-sample columns.
        const auto samples = pyramid->columns(0, 10, 20, 400);
        QCOMPARE(int(samples.size()), 10);
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(samples[size_t(i)].start, qint64(10 + i));
            QCOMPARE(samples[size_t(i)].end, qint64(11 + i));
            QCOMPARE(samples[size_t(i)].minimum, float(10 + i) / 1024.0f);
            QCOMPARE(samples[size_t(i)].maximum, float(10 + i) / 1024.0f);
        }
        CancellationSource cancel;
        cancel.cancel();
        QVERIFY(!AudioPeakPyramid::build(buffer, cancel.token()));
    }

    void spectrogramPlacesAKnownToneInItsBand()
    {
        const auto buffer = tones(48000, {{1500.0, 1.0}});
        AudioSpectrogramRequest request;
        request.start = 0;
        request.end = 48000;
        request.columns = 8;
        request.rows = 48; // 500 Hz bands on a linear axis
        request.minimumHz = 0.0;
        request.maximumHz = 24000.0;
        request.logarithmic = false;
        request.channel = 0;
        int stride = 0;
        QCOMPARE(AudioSpectrogram::normalize(request, *buffer, &stride), QString());
        QCOMPARE(stride, 1);
        const auto image = AudioSpectrogram::render(*buffer, request, stride, [&](qint64 index) {
            return AudioSpectrogram::computeTile(*buffer, 0, request.fftSize, request.hop, index);
        });
        QVERIFY(image);
        QCOMPARE(image->boundaries.front(), qint64(0));
        QCOMPARE(image->boundaries[1], qint64(6000));
        QCOMPARE(image->boundaries.back(), qint64(48000));
        QCOMPARE(image->bandEdges[3], 1500.0);
        for (int column = 1; column < 7; ++column) { // away from the zero-padded ends
            QVERIFY(std::fabs(image->at(column, 3)) < 0.01f);          // [1500, 2000): bin 64
            QVERIFY(std::fabs(image->at(column, 2) + 6.0206f) < 0.01f); // [1000, 1500): bin 63
            QVERIFY(image->at(column, 10) < -100.0f);
        }
        // Log axis: the band holding 1500 Hz reads 0 dBFS.
        AudioSpectrogramRequest log = request;
        log.minimumHz = 20.0;
        log.maximumHz = 20000.0;
        log.logarithmic = true;
        log.rows = 30;
        log.hop = 512;
        QCOMPARE(AudioSpectrogram::normalize(log, *buffer, &stride), QString());
        const auto logImage = AudioSpectrogram::render(*buffer, log, stride, [&](qint64 index) {
            return AudioSpectrogram::computeTile(*buffer, 0, log.fftSize, log.hop, index);
        });
        QVERIFY(logImage);
        int row = -1;
        for (int r = 0; r < log.rows; ++r)
            if (logImage->bandEdges[size_t(r)] <= 1500.0 && 1500.0 < logImage->bandEdges[size_t(r) + 1]) row = r;
        QVERIFY(row >= 0);
        QVERIFY(std::fabs(logImage->at(4, row)) < 0.01f);
    }

    void spectrumTilesJoinWithoutSeams()
    {
        auto buffer = makeBuffer(32768);
        buffer->samples[16384] = 1.0f; // centre of frame 32: the first frame of tile 1
        const auto tile0 = AudioSpectrogram::computeTile(*buffer, 0, 2048, 512, 0);
        const auto tile1 = AudioSpectrogram::computeTile(*buffer, 0, 2048, 512, 1);
        QVERIFY(tile0 && tile1);
        const std::vector<float> f31 = magnitudes(*buffer, 2048, 31 * 512);
        const std::vector<float> f32 = magnitudes(*buffer, 2048, 32 * 512);
        for (int k = 0; k < 1025; ++k) {
            QCOMPARE(tile0->frame(31)[k], f31[size_t(k)]);
            QCOMPARE(tile1->frame(0)[k], f32[size_t(k)]);
        }
        AudioSpectrogramRequest request;
        request.start = 0;
        request.end = 32768;
        request.columns = 16; // 2048 samples each: column 8 starts at the impulse
        request.rows = 4;
        request.minimumHz = 0.0;
        request.maximumHz = 24000.0;
        request.logarithmic = false;
        request.channel = 0;
        int stride = 0;
        QCOMPARE(AudioSpectrogram::normalize(request, *buffer, &stride), QString());
        // Tiles computed on demand or handed in precomputed give the same image.
        const auto onDemand = AudioSpectrogram::render(*buffer, request, stride, [&](qint64 index) {
            return AudioSpectrogram::computeTile(*buffer, 0, 2048, 512, index);
        });
        const std::vector<AudioSpectrumTilePtr> ready{tile0, tile1, AudioSpectrogram::computeTile(*buffer, 0, 2048, 512, 2)};
        const auto cached = AudioSpectrogram::render(*buffer, request, stride, [&](qint64 index) {
            return ready.at(size_t(index));
        });
        QVERIFY(onDemand && cached);
        QCOMPARE(onDemand->decibels, cached->decibels);
        const float impulse = AudioDsp::amplitudeToDecibels(4.0 / 2048); // flat at 2/Σw
        QVERIFY(std::fabs(onDemand->at(8, 3) - impulse) < 0.01f);
        QCOMPARE(onDemand->at(3, 3), -240.0f); // silence reads the -240 dB floor
    }

    void spectrogramRequestsStayBounded()
    {
        auto buffer = makeBuffer(480000);
        AudioSpectrogramRequest request;
        request.end = 480000;
        request.columns = 64;
        request.rows = 400;
        request.maxFrames = 64;
        request.channel = -1;
        int stride = 0;
        QCOMPARE(AudioSpectrogram::normalize(request, *buffer, &stride), QString());
        QCOMPARE(stride, 16); // 938 frames at hop 512 → 59 at hop 8192
        QCOMPARE(request.hop, 8192);
        QVERIFY(request.columns * request.rows <= AudioSpectrogram::kMaxCells);
        QCOMPARE(request.columns, 30); // 12000 / 400
        request.maximumHz = 40000.0;
        request.hop = 512; // normalize() left the strided hop in the request
        QCOMPARE(AudioSpectrogram::normalize(request, *buffer, &stride), QString());
        QCOMPARE(request.maximumHz, 24000.0); // clamped to Nyquist
        AudioSpectrogramRequest bad;
        bad.fftSize = 1000;
        QVERIFY(!AudioSpectrogram::normalize(bad, *buffer, &stride).isEmpty());
        CancellationSource cancel;
        cancel.cancel();
        QVERIFY(!AudioSpectrogram::computeTile(*buffer, 0, 2048, 512, 0, cancel.token()));
        QVERIFY(!AudioSpectrogram::render(*buffer, request, stride, [](qint64) { return AudioSpectrumTilePtr(); },
                                          cancel.token()));
    }
};

QTEST_GUILESS_MAIN(TestAudioDsp)
#include "tst_audio_dsp.moc"
