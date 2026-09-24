// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>
#include <optional>

// AGENT-CONTRACT (docs/video-scopes.md, D-127): the signal conventions a
// scope snapshot states and every consumer (VideoScopes, tests) reads.
//   matrix: the Y'CbCr coefficients (Rec.709, Rec.601, Rec.2020 NCL);
//   range: the 8-bit code mapping of normalized R'G'B', Y' and Cb/Cr --
//     full: 0-255 with chroma 128 + 255C (clamped),
//     limited: 16 + 219V for R'G'B'/Y', 128 + 224C for Cb/Cr;
//   space: "srgb" converts a tagged frame to encoded sRGB first (untagged
//     frames are taken as encoded sRGB); "signal" measures the delivered
//     encoded values unchanged and reports the tag's transfer.
// Measurements are of the encoded (non-linear) signal, never linear light.
// Waveform/parade rows and histogram bins index 8-bit codes (row 0 = 255).
struct VideoScopeConvention {
    enum class Matrix { Bt709, Bt601, Bt2020 };
    enum class Range { Full, Limited };
    enum class Space { Srgb, Signal };
    Matrix matrix = Matrix::Bt709;
    Range range = Range::Full;
    Space space = Space::Srgb;

    static std::optional<Matrix> matrixFrom(const QString &name);  // bt709, bt601, bt2020
    static std::optional<Range> rangeFrom(const QString &name);    // full, limited
    static std::optional<Space> spaceFrom(const QString &name);    // srgb, signal
    static QString matrixName(Matrix matrix);
    static QString rangeName(Range range);
    static QString spaceName(Space space);
    double kr() const;
    double kb() const;
    // 8-bit codes of normalized values under this convention.
    int lumaCode(double value) const;   // Y' and R'G'B' in [0, 1]
    int chromaCode(double value) const; // Cb/Cr in [-0.5, 0.5]
};

namespace VideoScopeAnalysis {
inline constexpr int kMaxPixels = 16 * 1024 * 1024;
inline constexpr int kHistogramBins = 128;
inline constexpr int kWaveColumns = 128;
inline constexpr int kWaveRows = 64;
inline constexpr int kVectorSide = 64;
// A snapshot map (docs/video-scopes.md "Snapshot"); {valid:false, error}
// when the frame cannot be analyzed.
QVariantMap analyze(const QImage &frame, const QString &frameId, const VideoScopeConvention &convention);
// Graticule targets: the 75% and 100% primaries and secondaries as codes and
// normalized vectorscope positions under `convention`.
QVariantList vectorTargets(const VideoScopeConvention &convention);
} // namespace VideoScopeAnalysis
