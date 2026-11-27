#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lwe {

// Detection de beat et estimation du tempo a partir d'une fonction de "nouveaute" (flux spectral),
// fournie a cadence fixe (une valeur par analyse FFT, ~94 par seconde). Logique pure, sans Win32 : testee
// par ctest sur des signaux synthetiques.
//
// - Onset : pic local de la nouveaute au-dessus d'un seuil adaptatif (moyenne + k * ecart-type sur ~1,5 s),
//   avec un intervalle minimal entre deux beats. Decision avec une analyse de retard (~11 ms).
// - Tempo : autocorrelation de la nouveaute sur ~6 s, recalculee 2 fois par seconde, ponderee vers 120 BPM
//   (evite les erreurs d'octave), affinee par interpolation parabolique puis lissee.
// - Intensite : 1 au beat, decroissance exponentielle (un quart de periode si le tempo est connu).
class BeatDetector {
public:
    explicit BeatDetector(float frameRate);

    // 0,25 (peu de beats, seulement les plus nets) a 2 (tres sensible) ; 1 par defaut.
    void SetSensitivity(float sensitivity) noexcept;

    // Une valeur de nouveaute (>= 0) par analyse. Retourne true si un beat vient d'etre detecte.
    bool Process(float novelty);
    // Temps ecoule sans analyse (rien ne joue) : l'intensite retombe, le tempo finit par etre oublie.
    void Advance(float seconds);

    float Bpm() const noexcept { return bpm_; }          // 0 = inconnu
    float Confidence() const noexcept { return confidence_; }
    float Intensity() const noexcept { return intensity_; }
    float SecondsSinceBeat() const noexcept { return sinceBeat_; }
    uint32_t BeatCount() const noexcept { return beatCount_; }
    // Derniere nouveaute recue et seuil courant (diagnostic, vumetre).
    float LastNovelty() const noexcept { return filled_ ? History(0) : 0.0f; }
    float Threshold() const noexcept { return threshold_; }

    static constexpr float kMinBpm = 60.0f;
    static constexpr float kMaxBpm = 200.0f;

private:
    void EstimateTempo();
    void UpdateIntensity();
    float History(size_t ago) const noexcept;  // 0 = valeur la plus recente

    float frameRate_;
    float frameSeconds_;
    float sensitivity_ = 1.0f;

    std::vector<float> history_;  // tampon circulaire de la nouveaute (~6 s)
    size_t writePos_ = 0;
    size_t filled_ = 0;
    size_t sinceTempo_ = 0;

    size_t thresholdWindow_;  // fenetre du seuil adaptatif (analyses)

    float threshold_ = 0.0f;
    float peakEnvelope_ = 0.0f;  // maximum recent de la nouveaute, decroissance lente
    float sinceBeat_ = 1e6f;
    float intensity_ = 0.0f;
    uint32_t beatCount_ = 0;

    float bpm_ = 0.0f;
    float confidence_ = 0.0f;
    float candidateBpm_ = 0.0f;  // nouveau tempo en attente de confirmation
    std::vector<float> autocorrelation_;
};

}  // namespace lwe
