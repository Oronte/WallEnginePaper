#include "audio/BeatDetector.h"

#include <algorithm>
#include <cmath>

namespace lwe {
namespace {

constexpr float kHistorySeconds = 6.0f;      // fenetre de l'autocorrelation (tempo)
constexpr float kThresholdSeconds = 1.5f;    // fenetre du seuil adaptatif
constexpr float kTempoIntervalSeconds = 0.5f;
constexpr float kMinTempoSeconds = 3.0f;     // historique minimal avant une premiere estimation
constexpr float kMinBeatGapSeconds = 0.2f;   // 300 BPM au plus
constexpr float kForgetSeconds = 5.0f;       // sans beat : le tempo est oublie
constexpr float kMinNovelty = 0.3f;          // plancher absolu (unites : dizaines de dB de hausse moyenne)
constexpr float kPeakMemorySeconds = 3.0f;   // memoire des attaques fortes recentes
constexpr float kPeakFraction = 0.3f;        // un beat atteint au moins 30 % des attaques fortes recentes
constexpr float kPreferredBpm = 120.0f;      // centre de la ponderation (evite les erreurs d'octave)
constexpr float kPreferenceOctaves = 0.9f;

}  // namespace

BeatDetector::BeatDetector(float frameRate)
    : frameRate_(std::max(1.0f, frameRate)),
      frameSeconds_(1.0f / std::max(1.0f, frameRate)),
      history_(static_cast<size_t>(kHistorySeconds * std::max(1.0f, frameRate)), 0.0f),
      thresholdWindow_(static_cast<size_t>(kThresholdSeconds * std::max(1.0f, frameRate))) {}

void BeatDetector::SetSensitivity(float sensitivity) noexcept {
    sensitivity_ = std::clamp(sensitivity, 0.25f, 2.0f);
}

float BeatDetector::History(size_t ago) const noexcept {
    const size_t size = history_.size();
    return history_[(writePos_ + size - 1 - (ago % size)) % size];
}

bool BeatDetector::Process(float novelty) {
    novelty = std::max(0.0f, std::isfinite(novelty) ? novelty : 0.0f);
    history_[writePos_] = novelty;
    peakEnvelope_ = std::max(novelty, peakEnvelope_ * std::exp(-frameSeconds_ / kPeakMemorySeconds));
    writePos_ = (writePos_ + 1) % history_.size();
    filled_ = std::min(filled_ + 1, history_.size());
    sinceBeat_ += frameSeconds_;

    bool beat = false;
    if (filled_ >= 3) {
        // Seuil adaptatif sur la fenetre recente (hors candidat).
        const size_t window = std::min(thresholdWindow_, filled_ - 1);
        double sum = 0.0;
        double sumSq = 0.0;
        for (size_t i = 1; i <= window; ++i) {
            const double v = History(i);
            sum += v;
            sumSq += v * v;
        }
        const double mean = sum / static_cast<double>(window);
        const double deviation = std::sqrt(std::max(0.0, sumSq / static_cast<double>(window) - mean * mean));
        // Trois garde-fous : au-dessus de la dispersion recente, nettement au-dessus de la moyenne (bruit
        // stationnaire), et pas trop loin des attaques fortes recentes (charleston entre deux kicks).
        const double k = 1.5 / sensitivity_;
        const double ratio = 1.0 + 1.0 / sensitivity_;
        const double threshold = std::max({mean + k * deviation, mean * ratio,
                                           static_cast<double>(peakEnvelope_) * kPeakFraction / sensitivity_}) +
                                 kMinNovelty / sensitivity_;
        threshold_ = static_cast<float>(threshold);

        // Pic local : le candidat (analyse precedente) domine ses deux voisins.
        const float candidate = History(1);
        const bool peak = candidate > History(2) && candidate >= History(0);
        const float period = bpm_ > 0.0f ? 60.0f / bpm_ : 0.0f;
        const float minGap = std::max(kMinBeatGapSeconds, confidence_ > 0.3f ? 0.45f * period : 0.0f);
        if (peak && candidate > threshold && sinceBeat_ - frameSeconds_ >= minGap) {
            beat = true;
            ++beatCount_;
            sinceBeat_ = frameSeconds_;  // le beat a eu lieu a l'analyse precedente
        }
    }

    if (++sinceTempo_ >= static_cast<size_t>(kTempoIntervalSeconds * frameRate_) &&
        static_cast<float>(filled_) >= kMinTempoSeconds * frameRate_) {
        sinceTempo_ = 0;
        EstimateTempo();
    }
    if (sinceBeat_ > kForgetSeconds) {
        bpm_ = 0.0f;
        confidence_ = 0.0f;
        candidateBpm_ = 0.0f;
    }
    UpdateIntensity();
    return beat;
}

void BeatDetector::Advance(float seconds) {
    if (!(seconds > 0.0f)) {
        return;
    }
    sinceBeat_ += seconds;
    if (sinceBeat_ > kForgetSeconds) {
        bpm_ = 0.0f;
        confidence_ = 0.0f;
        candidateBpm_ = 0.0f;
    }
    UpdateIntensity();
}

void BeatDetector::UpdateIntensity() {
    // Decroissance sur un quart de periode : le pulse est retombe bien avant le beat suivant.
    const float tau = bpm_ > 0.0f ? 0.25f * 60.0f / bpm_ : 0.12f;
    intensity_ = beatCount_ == 0 ? 0.0f : std::exp(-std::max(0.0f, sinceBeat_ - frameSeconds_) / tau);
}

void BeatDetector::EstimateTempo() {
    // Fonction d'onset centree et redressee (du plus ancien au plus recent).
    const size_t n = filled_;
    std::vector<float> onset(n);
    double mean = 0.0;
    for (size_t i = 0; i < n; ++i) {
        onset[i] = History(n - 1 - i);
        mean += onset[i];
    }
    mean /= static_cast<double>(n);
    for (auto& v : onset) {
        v = std::max(0.0f, v - static_cast<float>(mean));
    }

    const auto lagFor = [&](float bpm) { return 60.0f * frameRate_ / bpm; };
    const size_t minLag = static_cast<size_t>(std::floor(lagFor(kMaxBpm)));
    const size_t maxLag = static_cast<size_t>(std::ceil(lagFor(kMinBpm)));
    const size_t maxNeeded = std::min(n - 1, 2 * maxLag + 1);
    autocorrelation_.assign(maxNeeded + 1, 0.0f);
    for (size_t lag = 0; lag <= maxNeeded; ++lag) {
        double acc = 0.0;
        for (size_t t = lag; t < n; ++t) {
            acc += static_cast<double>(onset[t]) * onset[t - lag];
        }
        autocorrelation_[lag] = static_cast<float>(acc / static_cast<double>(n - lag));
    }
    if (autocorrelation_[0] <= 1e-9f) {
        return;
    }

    // Score : periode + sa double (renforce la vraie pulsation), pondere vers ~120 BPM.
    const auto score = [&](size_t lag) -> float {
        if (lag < 1 || lag > maxNeeded) {
            return 0.0f;
        }
        const float harmonic = 2 * lag <= maxNeeded ? autocorrelation_[2 * lag] : 0.0f;
        const float bpm = 60.0f * frameRate_ / static_cast<float>(lag);
        const float octaves = std::log2(bpm / kPreferredBpm) / kPreferenceOctaves;
        return (autocorrelation_[lag] + 0.5f * harmonic) * std::exp(-0.5f * octaves * octaves);
    };
    size_t best = 0;
    float bestScore = 0.0f;
    for (size_t lag = std::max<size_t>(minLag, 2); lag <= std::min(maxLag, maxNeeded); ++lag) {
        const float s = score(lag);
        if (s > bestScore) {
            bestScore = s;
            best = lag;
        }
    }
    if (best == 0) {
        return;
    }
    // Interpolation parabolique autour du maximum (precision sub-analyse).
    float lag = static_cast<float>(best);
    const float left = score(best - 1);
    const float right = score(best + 1);
    const float denominator = left - 2.0f * bestScore + right;
    if (std::fabs(denominator) > 1e-12f) {
        lag += std::clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f);
    }
    const float estimate = 60.0f * frameRate_ / lag;
    const float confidence = std::clamp(autocorrelation_[best] / autocorrelation_[0], 0.0f, 1.0f);
    if (confidence < 0.1f || estimate < kMinBpm || estimate > kMaxBpm) {
        return;
    }
    confidence_ = confidence_ * 0.5f + confidence * 0.5f;

    // Lissage : petites variations integrees doucement ; un vrai changement doit etre confirme deux fois.
    const auto close = [](float a, float b) { return a > 0.0f && std::fabs(a - b) / a < 0.04f; };
    if (bpm_ <= 0.0f) {
        bpm_ = estimate;
    } else if (close(bpm_, estimate)) {
        bpm_ += (estimate - bpm_) * 0.3f;
    } else if (close(candidateBpm_, estimate)) {
        bpm_ = estimate;
        candidateBpm_ = 0.0f;
    } else {
        candidateBpm_ = estimate;
    }
}

}  // namespace lwe
