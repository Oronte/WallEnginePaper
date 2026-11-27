#include "audio/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lwe {
namespace {

constexpr float kMinFrequency = 40.0f;
constexpr float kMaxFrequency = 16000.0f;
constexpr float kFloorDb = -72.0f;  // en dessous : bande a zero
constexpr float kRangeDb = 60.0f;   // -72 dB -> 0, -12 dB -> 1
constexpr float kAttack = 0.65f;    // part de la nouvelle valeur quand ca monte
constexpr float kRelease = 0.86f;   // part de l'ancienne valeur quand ca descend

// Gain automatique (voir SetAutoGain).
constexpr float kAgcTargetDb = -14.0f;   // niveau RMS d'un morceau masterise "fort"
constexpr float kAgcMaxGainDb = 18.0f;
constexpr float kAgcGateDb = -60.0f;     // en dessous : silence, l'enveloppe est gelee
constexpr float kAgcAttack = 0.5f;       // par analyse (~94/s) : monte en quelques centiemes de seconde
constexpr float kAgcRelease = 0.0027f;   // par analyse : constante de temps ~4 s

float Normalize(float amplitude, float offsetDb) {
    const float db = 20.0f * std::log10(amplitude + 1e-9f) + offsetDb;
    return std::clamp((db - kFloorDb) / kRangeDb, 0.0f, 1.0f);
}

float Smooth(float previous, float target) {
    return target > previous ? previous + (target - previous) * kAttack
                             : previous * kRelease + target * (1.0f - kRelease);
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(uint32_t sampleRate)
    : sampleRate_(std::max<uint32_t>(8000, sampleRate)), history_(kFftSize, 0.0f), window_(kFftSize),
      twiddles_(kFftSize / 2), work_(kFftSize), envelopeDb_(kAgcTargetDb) {
    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    for (size_t i = 0; i < kFftSize; ++i) {
        window_[i] = 0.5f - 0.5f * std::cos(twoPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1));
    }
    for (size_t i = 0; i < kFftSize / 2; ++i) {
        twiddles_[i] = std::polar(1.0f, -twoPi * static_cast<float>(i) / static_cast<float>(kFftSize));
    }

    // Bornes des bandes, reparties en echelle logarithmique ; chaque bande couvre au moins un bin.
    const float binWidth = static_cast<float>(sampleRate_) / static_cast<float>(kFftSize);
    const float maxFrequency = std::min(kMaxFrequency, sampleRate_ * 0.5f - binWidth);
    size_t previous = 1;
    for (size_t i = 0; i <= kAudioBandCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kAudioBandCount);
        const float frequency = kMinFrequency * std::pow(maxFrequency / kMinFrequency, t);
        size_t bin = static_cast<size_t>(std::floor(frequency / binWidth));
        bin = std::clamp<size_t>(bin, 1, kFftSize / 2 - 1);
        if (i > 0 && bin <= previous) {
            bin = std::min(previous + 1, kFftSize / 2);
        }
        bandEdges_[i] = bin;
        previous = bin;
    }
    for (size_t i = 0; i < kAudioBandCount; ++i) {
        const float center = binWidth * 0.5f * static_cast<float>(bandEdges_[i] + bandEdges_[i + 1]);
        bandTilt_[i] = 3.0f * std::log2(std::max(center, 1.0f) / 1000.0f);  // la musique perd ~3 dB/octave
    }
}

bool SpectrumAnalyzer::Push(const float* samples, size_t count) {
    bool analyzed = false;
    for (size_t i = 0; i < count; ++i) {
        history_[writePos_] = samples[i];
        writePos_ = (writePos_ + 1) % kFftSize;
        blockEnergy_ += static_cast<double>(samples[i]) * samples[i];
        ++blockCount_;
        if (++sinceAnalysis_ >= kHop) {
            sinceAnalysis_ = 0;
            Analyze();
            analyzed = true;
        }
    }
    return analyzed;
}

float SpectrumAnalyzer::BandLowFrequency(size_t band) const noexcept {
    return static_cast<float>(bandEdges_[std::min(band, kAudioBandCount - 1)]) * sampleRate_ / kFftSize;
}

float SpectrumAnalyzer::BandHighFrequency(size_t band) const noexcept {
    const size_t i = std::min(band, kAudioBandCount - 1);
    return static_cast<float>(std::max(bandEdges_[i + 1], bandEdges_[i] + 1)) * sampleRate_ / kFftSize;
}

void SpectrumAnalyzer::Decay(float factor) {
    for (auto& band : spectrum_.bands) {
        band *= factor;
    }
    spectrum_.level *= factor;
}

void SpectrumAnalyzer::Fft(std::vector<std::complex<float>>& data, const std::vector<std::complex<float>>& twiddles) {
    const size_t n = data.size();
    // Permutation par inversion de bits.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    // Papillons de Cooley-Tukey, facteurs precalcules.
    for (size_t length = 2; length <= n; length <<= 1) {
        const size_t half = length / 2;
        const size_t step = n / length;
        for (size_t start = 0; start < n; start += length) {
            for (size_t k = 0; k < half; ++k) {
                const std::complex<float> t = twiddles[k * step] * data[start + k + half];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
        }
    }
}

void SpectrumAnalyzer::Analyze() {
    // Les kFftSize derniers echantillons, du plus ancien au plus recent, fenetres.
    for (size_t i = 0; i < kFftSize; ++i) {
        work_[i] = std::complex<float>(history_[(writePos_ + i) % kFftSize] * window_[i], 0.0f);
    }
    Fft(work_, twiddles_);

    // Niveau RMS du bloc ecoule et gain automatique.
    const float rms = blockCount_ ? static_cast<float>(std::sqrt(blockEnergy_ / static_cast<double>(blockCount_))) : 0.0f;
    const float rmsDb = 20.0f * std::log10(rms + 1e-9f);
    if (rmsDb > kAgcGateDb) {
        const float coefficient = rmsDb > envelopeDb_ ? kAgcAttack : kAgcRelease;
        envelopeDb_ += (rmsDb - envelopeDb_) * coefficient;
        gainDb_ = std::clamp(kAgcTargetDb - envelopeDb_, 0.0f, kAgcMaxGainDb);
    }
    const float gainDb = GainDb();

    // Amplitude d'une sinusoide : |X| * 2 / somme(fenetre), avec somme(Hann) = N/2.
    const float scale = 4.0f / static_cast<float>(kFftSize);
    for (size_t band = 0; band < kAudioBandCount; ++band) {
        float peak = 0.0f;
        for (size_t bin = bandEdges_[band]; bin < std::max(bandEdges_[band + 1], bandEdges_[band] + 1); ++bin) {
            peak = std::max(peak, std::abs(work_[bin]) * scale);
        }
        spectrum_.bands[band] = Smooth(spectrum_.bands[band], Normalize(peak, bandTilt_[band] + gainDb));
    }

    const float level = std::clamp((rmsDb + gainDb + 60.0f) / 54.0f, 0.0f, 1.0f);
    spectrum_.level = Smooth(spectrum_.level, level);
    blockEnergy_ = 0.0;
    blockCount_ = 0;
}

}  // namespace lwe
