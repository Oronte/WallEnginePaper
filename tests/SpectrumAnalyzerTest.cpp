// Tests de SpectrumAnalyzer : des sinusoides pures doivent allumer la bonne bande, le silence aucune.
// Lance par `ctest` (cible WallEnginePaperTests).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "audio/SpectrumAnalyzer.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    std::printf("[%s] %s\n", condition ? "OK" : "ECHEC", what);
    if (!condition) {
        ++g_failures;
    }
}

constexpr uint32_t kRate = 48000;

std::vector<float> Sine(float frequency, float amplitude, size_t count) {
    std::vector<float> samples(count);
    for (size_t i = 0; i < count; ++i) {
        samples[i] = amplitude * std::sin(2.0f * std::numbers::pi_v<float> * frequency * static_cast<float>(i) / kRate);
    }
    return samples;
}

size_t PeakBand(const lwe::AudioSpectrum& spectrum) {
    return static_cast<size_t>(std::max_element(spectrum.bands.begin(), spectrum.bands.end()) - spectrum.bands.begin());
}

// Bande attendue pour une frequence (meme repartition logarithmique que l'analyseur : 40 Hz -> 16 kHz).
size_t ExpectedBand(float frequency) {
    const float t = std::log(frequency / 40.0f) / std::log(16000.0f / 40.0f);
    return static_cast<size_t>(std::clamp(t * 64.0f, 0.0f, 63.0f));
}

}  // namespace

int main() {
    {
        lwe::SpectrumAnalyzer analyzer(kRate);
        const auto silence = std::vector<float>(kRate, 0.0f);
        analyzer.Push(silence.data(), silence.size());
        const auto& s = analyzer.Spectrum();
        Check(*std::max_element(s.bands.begin(), s.bands.end()) == 0.0f && s.level == 0.0f, "silence : spectre nul");
    }

    for (const float frequency : {100.0f, 440.0f, 1000.0f, 5000.0f, 12000.0f}) {
        lwe::SpectrumAnalyzer analyzer(kRate);
        const auto tone = Sine(frequency, 0.5f, kRate / 2);
        analyzer.Push(tone.data(), tone.size());
        const auto& s = analyzer.Spectrum();
        const size_t peak = PeakBand(s);
        const size_t expected = ExpectedBand(frequency);
        // Le ton doit tomber dans la plage de la bande la plus forte (a un bin pres : fuite de la fenetre).
        const float binWidth = static_cast<float>(kRate) / lwe::SpectrumAnalyzer::kFftSize;
        const float low = analyzer.BandLowFrequency(peak) - binWidth;
        const float high = analyzer.BandHighFrequency(peak) + binWidth;
        char label[200];
        std::snprintf(label, sizeof(label), "%.0f Hz : pic en bande %zu [%.0f-%.0f Hz], valeur %.2f, niveau %.2f",
                      frequency, peak, analyzer.BandLowFrequency(peak), analyzer.BandHighFrequency(peak),
                      s.bands[peak], s.level);
        Check(frequency >= low && frequency <= high && s.bands[peak] > 0.6f && s.level > 0.5f, label);

        // Une bande eloignee du ton doit rester nettement plus basse (pas de fuite spectrale massive).
        const size_t distant = expected > 32 ? expected - 24 : expected + 24;
        std::snprintf(label, sizeof(label), "%.0f Hz : bande %zu eloignee basse (%.2f)", frequency, distant, s.bands[distant]);
        Check(s.bands[distant] < s.bands[peak] * 0.5f, label);
    }

    {
        lwe::SpectrumAnalyzer analyzer(kRate);
        const auto tone = Sine(440.0f, 0.5f, kRate / 2);
        analyzer.Push(tone.data(), tone.size());
        const float before = analyzer.Spectrum().bands[PeakBand(analyzer.Spectrum())];
        for (int i = 0; i < 50; ++i) {
            analyzer.Decay(0.88f);
        }
        const float after = *std::max_element(analyzer.Spectrum().bands.begin(), analyzer.Spectrum().bands.end());
        Check(after < before * 0.01f, "retombee vers zero apres l'arret du son");
    }

    {
        // Un son fort doit donner un niveau plus eleve qu'un son faible (gain automatique coupe).
        lwe::SpectrumAnalyzer loud(kRate);
        lwe::SpectrumAnalyzer quiet(kRate);
        loud.SetAutoGain(false);
        quiet.SetAutoGain(false);
        const auto loudTone = Sine(1000.0f, 0.8f, kRate / 2);
        const auto quietTone = Sine(1000.0f, 0.02f, kRate / 2);
        loud.Push(loudTone.data(), loudTone.size());
        quiet.Push(quietTone.data(), quietTone.size());
        Check(loud.Spectrum().level > quiet.Spectrum().level + 0.2f, "niveau proportionnel au volume");
    }

    {
        // Gain automatique : une source calme (-36 dBFS) est remontee, un son fort n'est pas touche,
        // le silence reste nul.
        lwe::SpectrumAnalyzer withGain(kRate);
        lwe::SpectrumAnalyzer without(kRate);
        without.SetAutoGain(false);
        const auto quietTone = Sine(440.0f, 0.016f, kRate * 15);
        withGain.Push(quietTone.data(), quietTone.size());
        without.Push(quietTone.data(), quietTone.size());
        const float boosted = withGain.Spectrum().bands[PeakBand(withGain.Spectrum())];
        const float raw = without.Spectrum().bands[PeakBand(without.Spectrum())];
        char label[160];
        std::snprintf(label, sizeof(label), "gain auto : source calme remontee (%.2f -> %.2f, %+.1f dB)", raw, boosted,
                      withGain.GainDb());
        Check(boosted > raw + 0.2f && withGain.GainDb() > 15.0f, label);

        lwe::SpectrumAnalyzer loud(kRate);
        const auto loudTone = Sine(440.0f, 0.5f, kRate * 5);
        loud.Push(loudTone.data(), loudTone.size());
        Check(loud.GainDb() == 0.0f, "gain auto : aucun gain sur un son fort");

        const auto silence = std::vector<float>(kRate * 5, 0.0f);
        withGain.Push(silence.data(), silence.size());
        for (int i = 0; i < 100; ++i) {
            withGain.Decay(0.88f);
        }
        Check(withGain.Spectrum().level < 0.01f && withGain.GainDb() > 15.0f, "gain auto : gele et sans effet en silence");
    }

    std::printf("\n%s (%d echec(s))\n", g_failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT", g_failures);
    return g_failures == 0 ? 0 : 1;
}
