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

// Flux spectral : spectre en "dizaines de dB" au-dessus de -80 dBFS ; poids des zones dans la nouveaute
// (les graves portent le rythme, les cymbales a large bande ne doivent pas dominer).
constexpr float kFluxFloorDb = -80.0f;
constexpr float kZoneWeights[3] = {1.0f, 0.4f, 0.1f};

float Normalize(float amplitude, float offsetDb) {
    const float db = 20.0f * std::log10(amplitude + 1e-9f) + offsetDb;
    return std::clamp((db - kFloorDb) / kRangeDb, 0.0f, 1.0f);
}

float Smooth(float previous, float target) {
    return target > previous ? previous + (target - previous) * kAttack
                             : previous * kRelease + target * (1.0f - kRelease);
}

uint8_t ZoneOf(float frequency) {
    return frequency < SpectrumAnalyzer::kBassMaxHz ? 0 : (frequency < SpectrumAnalyzer::kMidMaxHz ? 1 : 2);
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(uint32_t sampleRate)
    : sampleRate_(std::max<uint32_t>(8000, sampleRate)),
      hopSeconds_(static_cast<float>(kHop) / static_cast<float>(std::max<uint32_t>(8000, sampleRate))),
      history_(kFftSize, 0.0f), window_(kFftSize), twiddles_(kFftSize / 2), work_(kFftSize),
      beat_(static_cast<float>(std::max<uint32_t>(8000, sampleRate)) / static_cast<float>(kHop)),
      envelopeDb_(kAgcTargetDb) {
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
        bandZone_[i] = ZoneOf(center);
    }

    maxBin_ = std::min(kFftSize / 2 - 1, static_cast<size_t>(maxFrequency / binWidth));
    previousLog_.assign(maxBin_ + 1, 0.0f);
    binZone_.resize(maxBin_ + 1);
    for (size_t bin = 0; bin <= maxBin_; ++bin) {
        binZone_[bin] = ZoneOf(static_cast<float>(bin) * binWidth);
    }
}

void SpectrumAnalyzer::SetSmoothing(float attackMs, float releaseMs) noexcept {
    attackSeconds_ = std::clamp(attackMs, 0.0f, 2000.0f) / 1000.0f;
    releaseSeconds_ = std::clamp(releaseMs, 0.0f, 5000.0f) / 1000.0f;
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

void SpectrumAnalyzer::SmoothZones(const std::array<float, 3>& target, float seconds) {
    float* zones[3] = {&spectrum_.bass, &spectrum_.mid, &spectrum_.treble};
    for (size_t z = 0; z < 3; ++z) {
        float& value = *zones[z];
        const float tau = target[z] > value ? attackSeconds_ : releaseSeconds_;
        const float blend = tau > 0.0f ? 1.0f - std::exp(-seconds / tau) : 1.0f;
        value += (target[z] - value) * blend;
    }
}

void SpectrumAnalyzer::PublishBeat() {
    spectrum_.beat = beat_.Intensity();
    spectrum_.bpm = beat_.Bpm();
    spectrum_.beatTime = beat_.SecondsSinceBeat();
    spectrum_.beatCount = beat_.BeatCount();
}

void SpectrumAnalyzer::Decay(float factor, float seconds) {
    for (auto& band : spectrum_.bands) {
        band *= factor;
    }
    spectrum_.level *= factor;
    SmoothZones({0.0f, 0.0f, 0.0f}, seconds);
    beat_.Advance(seconds);
    PublishBeat();
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
    std::array<float, 3> zoneMax{};
    std::array<float, 3> zoneSum{};
    std::array<float, 3> zoneCount{};
    for (size_t band = 0; band < kAudioBandCount; ++band) {
        float peak = 0.0f;
        for (size_t bin = bandEdges_[band]; bin < std::max(bandEdges_[band + 1], bandEdges_[band] + 1); ++bin) {
            peak = std::max(peak, std::abs(work_[bin]) * scale);
        }
        const float raw = Normalize(peak, bandTilt_[band] + gainDb);
        spectrum_.bands[band] = Smooth(spectrum_.bands[band], raw);
        const uint8_t zone = bandZone_[band];
        zoneMax[zone] = std::max(zoneMax[zone], raw);
        zoneSum[zone] += raw;
        zoneCount[zone] += 1.0f;
    }
    // Niveau d'une zone : melange du maximum (une composante forte suffit) et de la moyenne (largeur).
    std::array<float, 3> zones{};
    for (size_t z = 0; z < 3; ++z) {
        zones[z] = zoneCount[z] > 0.0f ? 0.6f * zoneMax[z] + 0.4f * zoneSum[z] / zoneCount[z] : 0.0f;
    }
    SmoothZones(zones, hopSeconds_);

    const float level = std::clamp((rmsDb + gainDb + 60.0f) / 54.0f, 0.0f, 1.0f);
    spectrum_.level = Smooth(spectrum_.level, level);
    blockEnergy_ = 0.0;
    blockCount_ = 0;

    // Flux spectral (nouveaute) : hausse du spectre logarithmique depuis l'analyse precedente, par zone.
    // Comparaison au maximum des bins voisins de l'analyse precedente ("SuperFlux") : les fluctuations
    // d'un son stationnaire (bruit, vibrato) ne comptent pas, seules les vraies attaques ressortent.
    std::array<float, 3> flux{};
    std::array<float, 3> fluxBins{};
    float before = previousLog_[0];
    for (size_t bin = 1; bin <= maxBin_; ++bin) {
        const float db = 20.0f * std::log10(std::abs(work_[bin]) * scale + 1e-9f) + gainDb;
        const float value = std::max(0.0f, db - kFluxFloorDb) / 10.0f;
        const float next = bin < maxBin_ ? previousLog_[bin + 1] : previousLog_[bin];
        const float reference = std::max({before, previousLog_[bin], next});
        if (hasPrevious_) {
            flux[binZone_[bin]] += std::max(0.0f, value - reference);
        }
        fluxBins[binZone_[bin]] += 1.0f;
        before = previousLog_[bin];
        previousLog_[bin] = value;
    }
    float novelty = 0.0f;
    for (size_t z = 0; z < 3; ++z) {
        if (fluxBins[z] > 0.0f) {
            novelty += kZoneWeights[z] * flux[z] / fluxBins[z];
        }
    }
    if (hasPrevious_) {
        beat_.Process(novelty);
    }
    hasPrevious_ = true;
    PublishBeat();
}

}  // namespace lwe
