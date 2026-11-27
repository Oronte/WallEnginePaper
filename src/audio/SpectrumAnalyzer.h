#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

// Transforme un flux audio mono en spectre de 64 bandes (jusqu'a 16 kHz), normalise en decibels, avec
// une attaque rapide et un relachement lent (rendu visuel agreable, sans scintillement).
// FFT radix-2 de 2048 echantillons, fenetre de Hann, nouvelle analyse tous les 512 echantillons
// (~94 analyses par seconde a 48 kHz).
// Repartition : logarithmique au-dessus d'environ 1 kHz ; en dessous, la resolution de la FFT (23,4 Hz
// par bin a 48 kHz) ne permet pas 34 bandes distinctes, chaque bande couvre alors un bin (pas de barres
// dupliquees). Les basses (environ 20 a 190 Hz) occupent les bandes 0 a 7.
class SpectrumAnalyzer {
public:
    static constexpr size_t kFftSize = 2048;
    static constexpr size_t kHop = 512;

    explicit SpectrumAnalyzer(uint32_t sampleRate);

    // Ajoute des echantillons mono ; retourne true si une nouvelle analyse a ete produite.
    bool Push(const float* samples, size_t count);
    // Aucune donnee (rien ne joue) : le spectre retombe doucement vers zero.
    void Decay(float factor);

    const AudioSpectrum& Spectrum() const noexcept { return spectrum_; }

    // Plage de frequences (Hz) couverte par une bande.
    float BandLowFrequency(size_t band) const noexcept;
    float BandHighFrequency(size_t band) const noexcept;

private:
    void Analyze();
    static void Fft(std::vector<std::complex<float>>& data, const std::vector<std::complex<float>>& twiddles);

    uint32_t sampleRate_;
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
    AudioSpectrum spectrum_;
};

}  // namespace lwe
