#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio/BeatDetector.h"
#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

// Transforme un flux audio mono en spectre de 64 bandes (jusqu'a 16 kHz), normalise en decibels, avec
// une attaque rapide et un relachement lent (rendu visuel agreable, sans scintillement).
// FFT radix-2 de 2048 echantillons, fenetre de Hann, nouvelle analyse tous les 512 echantillons
// (~94 analyses par seconde a 48 kHz).
// Repartition : logarithmique au-dessus d'environ 1 kHz ; en dessous, la resolution de la FFT (23,4 Hz
// par bin a 48 kHz) ne permet pas 34 bandes distinctes, chaque bande couvre alors un bin (pas de barres
// dupliquees). Les graves (environ 20 a 260 Hz) occupent les bandes 0 a 9.
//
// Analyse rythmique : niveaux graves / mediums / aigus (attaque et relachement en millisecondes), flux
// spectral (nouveaute) transmis a BeatDetector pour les beats et le tempo.
class SpectrumAnalyzer {
public:
    static constexpr size_t kFftSize = 2048;
    static constexpr size_t kHop = 512;

    // Limites des zones de frequences (Hz).
    static constexpr float kBassMaxHz = 250.0f;
    static constexpr float kMidMaxHz = 4000.0f;

    explicit SpectrumAnalyzer(uint32_t sampleRate);

    // Ajoute des echantillons mono ; retourne true si une nouvelle analyse a ete produite.
    bool Push(const float* samples, size_t count);
    // Aucune donnee (rien ne joue) pendant `seconds` : le spectre retombe doucement vers zero.
    void Decay(float factor, float seconds = 0.01f);

    const AudioSpectrum& Spectrum() const noexcept { return spectrum_; }
    const BeatDetector& Beats() const noexcept { return beat_; }

    // Plage de frequences (Hz) couverte par une bande.
    float BandLowFrequency(size_t band) const noexcept;
    float BandHighFrequency(size_t band) const noexcept;

    // Gain automatique : une source calme (musique classique, volume applicatif bas) est remontee
    // jusqu'a +18 dB pour occuper la meme plage visuelle qu'un morceau masterise fort. Jamais d'attenuation ;
    // le gain est gele pendant les silences (pas d'explosion a la reprise).
    void SetAutoGain(bool enabled) noexcept { autoGain_ = enabled; }
    float GainDb() const noexcept { return autoGain_ ? gainDb_ : 0.0f; }

    // Lissage des niveaux graves / mediums / aigus : constantes de temps de montee et de descente.
    void SetSmoothing(float attackMs, float releaseMs) noexcept;
    void SetBeatSensitivity(float sensitivity) noexcept { beat_.SetSensitivity(sensitivity); }

    // Duree d'une analyse (secondes) : kHop / frequence d'echantillonnage.
    float HopSeconds() const noexcept { return hopSeconds_; }

private:
    void Analyze();
    void SmoothZones(const std::array<float, 3>& target, float seconds);
    void PublishBeat();
    static void Fft(std::vector<std::complex<float>>& data, const std::vector<std::complex<float>>& twiddles);

    uint32_t sampleRate_;
    float hopSeconds_;
    std::vector<float> history_;  // tampon circulaire des derniers kFftSize echantillons
    size_t writePos_ = 0;
    size_t sinceAnalysis_ = 0;
    double blockEnergy_ = 0.0;
    size_t blockCount_ = 0;

    std::vector<float> window_;
    std::vector<std::complex<float>> twiddles_;
    std::vector<std::complex<float>> work_;
    std::array<size_t, kAudioBandCount + 1> bandEdges_{};  // indices de bins
    std::array<float, kAudioBandCount> bandTilt_{};       // compensation +3 dB/octave
    std::array<uint8_t, kAudioBandCount> bandZone_{};     // 0 graves, 1 mediums, 2 aigus
    AudioSpectrum spectrum_;

    // Flux spectral : spectre logarithmique precedent, zone de chaque bin.
    std::vector<float> previousLog_;
    std::vector<uint8_t> binZone_;
    size_t maxBin_ = 0;
    bool hasPrevious_ = false;

    float attackSeconds_ = 0.010f;
    float releaseSeconds_ = 0.200f;
    BeatDetector beat_;

    bool autoGain_ = true;
    float envelopeDb_;  // enveloppe lente du niveau RMS (dBFS), initialisee sur la cible
    float gainDb_ = 0.0f;
};

}  // namespace lwe
