// Tests de la detection de beat et du tempo sur des signaux synthetiques (clics, kicks, bruit, silence).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <numbers>
#include <random>
#include <vector>

#include "TestUtil.h"
#include "audio/SpectrumAnalyzer.h"

namespace {

using lwe::test::Check;

constexpr uint32_t kRate = 48000;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// Clic court (bruit decroissant de ~5 ms) a chaque temps.
std::vector<float> ClickTrack(float bpm, float seconds) {
    std::vector<float> out(static_cast<size_t>(seconds * kRate), 0.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const double period = 60.0 / bpm;
    for (double t = 0.25; t < seconds; t += period) {
        const size_t start = static_cast<size_t>(t * kRate);
        for (size_t k = 0; k < kRate / 200 && start + k < out.size(); ++k) {
            out[start + k] += 0.7f * noise(rng) * std::exp(-static_cast<float>(k) / (kRate * 0.0015f));
        }
    }
    return out;
}

// Kick (sinus glissant 150 -> 50 Hz) a chaque temps + charleston (bruit passe-haut) sur les contretemps
// + nappe continue.
std::vector<float> DrumLoop(float bpm, float seconds) {
    std::vector<float> out(static_cast<size_t>(seconds * kRate), 0.0f);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const double period = 60.0 / bpm;
    for (double t0 = 0.1; t0 < seconds; t0 += period) {
        const size_t start = static_cast<size_t>(t0 * kRate);
        float phase = 0.0f;
        for (size_t k = 0; k < kRate / 4 && start + k < out.size(); ++k) {
            const float t = static_cast<float>(k) / kRate;
            phase += kTwoPi * (50.0f + 100.0f * std::exp(-t * 30.0f)) / kRate;
            out[start + k] += 0.6f * std::sin(phase) * std::exp(-t * 12.0f);
        }
        // Charleston : bruit blanc derive trois fois (passe-haut ~18 dB/octave), energie surtout au-dessus de 5 kHz.
        const size_t hat = static_cast<size_t>((t0 + period / 2) * kRate);
        float d1 = 0.0f, d2 = 0.0f, d3 = 0.0f;
        for (size_t k = 0; k < kRate / 25 && hat + k < out.size(); ++k) {
            const float w = noise(rng);
            const float h1 = w - d1;
            const float h2 = h1 - d2;
            const float h3 = h2 - d3;
            d1 = w;
            d2 = h1;
            d3 = h2;
            out[hat + k] += 0.05f * h3 * std::exp(-static_cast<float>(k) / kRate * 120.0f);
        }
    }
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] += 0.05f * std::sin(kTwoPi * 440.0f * static_cast<float>(i) / kRate);  // nappe tenue
    }
    return out;
}

// Bruit rose (filtre de Paul Kellet), ~-20 dBFS RMS.
std::vector<float> PinkNoise(float seconds) {
    std::vector<float> out(static_cast<size_t>(seconds * kRate));
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& sample : out) {
        const float w = white(rng);
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;
        b5 = -0.7616f * b5 - w * 0.0168980f;
        sample = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.05f;
        b6 = w * 0.115926f;
    }
    return out;
}

struct Result {
    float bpm = 0.0f;
    uint32_t beats = 0;
    float maxBeat = 0.0f;
};

// Envoie le signal par paquets de 10 ms (comme la capture WASAPI) et renvoie l'etat final.
Result Run(lwe::SpectrumAnalyzer& analyzer, const std::vector<float>& signal) {
    Result result;
    const uint32_t before = analyzer.Spectrum().beatCount;
    constexpr size_t kChunk = kRate / 100;
    for (size_t i = 0; i < signal.size(); i += kChunk) {
        analyzer.Push(signal.data() + i, std::min(kChunk, signal.size() - i));
        result.maxBeat = std::max(result.maxBeat, analyzer.Spectrum().beat);
    }
    result.bpm = analyzer.Spectrum().bpm;
    result.beats = analyzer.Spectrum().beatCount - before;
    return result;
}

}  // namespace

void RunBeatDetectorTests() {
    lwe::test::Section("BeatDetector");
    char label[200];
    char* trace = nullptr;
    size_t traceLength = 0;
    if (_dupenv_s(&trace, &traceLength, "LWE_BEAT_TRACE") == 0 && trace) {
        // Diagnostic : LWE_BEAT_TRACE=1 affiche la nouveaute, le seuil et les beats d'une boucle de batterie.
        lwe::SpectrumAnalyzer analyzer(kRate);
        const auto signal = std::string(trace) == "pink" ? PinkNoise(4.0f) : DrumLoop(120.0f, 4.0f);
        for (size_t i = 0; i < signal.size(); i += lwe::SpectrumAnalyzer::kHop) {
            const uint32_t before = analyzer.Spectrum().beatCount;
            analyzer.Push(signal.data() + i, std::min<size_t>(lwe::SpectrumAnalyzer::kHop, signal.size() - i));
            std::printf("%6.3f nov %6.3f thr %6.3f %s\n", static_cast<double>(i) / kRate, analyzer.Beats().LastNovelty(),
                        analyzer.Beats().Threshold(), analyzer.Spectrum().beatCount != before ? "BEAT" : "");
        }
        free(trace);
    }

    for (const float bpm : {90.0f, 120.0f, 128.0f, 140.0f}) {
        lwe::SpectrumAnalyzer analyzer(kRate);
        const float seconds = 12.0f;
        const Result r = Run(analyzer, ClickTrack(bpm, seconds));
        const float expectedBeats = (seconds - 0.25f) * bpm / 60.0f;
        std::snprintf(label, sizeof(label), "clics a %.0f BPM : tempo %.1f, %u beats (attendu ~%.0f)", bpm, r.bpm,
                      r.beats, expectedBeats);
        Check(std::fabs(r.bpm - bpm) <= 2.0f && std::fabs(static_cast<float>(r.beats) - expectedBeats) <= 2.0f, label);
    }

    for (const float bpm : {100.0f, 120.0f, 128.0f}) {
        lwe::SpectrumAnalyzer analyzer(kRate);
        const float seconds = 12.0f;
        const Result r = Run(analyzer, DrumLoop(bpm, seconds));
        const float expectedBeats = (seconds - 0.1f) * bpm / 60.0f;
        std::snprintf(label, sizeof(label), "kick + charleston a %.0f BPM : tempo %.1f, %u beats (attendu ~%.0f)", bpm,
                      r.bpm, r.beats, expectedBeats);
        // Le charleston (contretemps) ne doit pas etre compte comme un beat.
        Check(std::fabs(r.bpm - bpm) <= 2.0f && std::fabs(static_cast<float>(r.beats) - expectedBeats) <= 3.0f, label);
    }

    {
        // L'intensite vaut ~1 au beat et retombe avant le suivant.
        lwe::SpectrumAnalyzer analyzer(kRate);
        const auto signal = ClickTrack(120.0f, 8.0f);
        Run(analyzer, signal);
        // Avance jusqu'a juste apres un beat, puis a mi-chemin du suivant.
        const auto more = ClickTrack(120.0f, 0.3f);  // un clic a 0,25 s
        float atBeat = 0.0f;
        float later = 1.0f;
        constexpr size_t kChunk = kRate / 100;
        for (size_t i = 0; i < more.size(); i += kChunk) {
            analyzer.Push(more.data() + i, std::min(kChunk, more.size() - i));
            if (analyzer.Spectrum().beatTime < 0.03f) {
                atBeat = std::max(atBeat, analyzer.Spectrum().beat);
            }
        }
        const auto gap = std::vector<float>(kRate / 4, 0.0f);  // 250 ms de silence (mi-periode)
        analyzer.Push(gap.data(), gap.size());
        later = analyzer.Spectrum().beat;
        std::snprintf(label, sizeof(label), "intensite : %.2f au beat, %.2f une demi-periode plus tard", atBeat, later);
        Check(atBeat > 0.8f && later < 0.25f, label);
    }

    {
        lwe::SpectrumAnalyzer analyzer(kRate);
        const Result r = Run(analyzer, std::vector<float>(kRate * 6, 0.0f));
        Check(r.beats == 0 && r.bpm == 0.0f && r.maxBeat == 0.0f, "silence : aucun beat, tempo inconnu");
    }

    {
        // Bruit rose stationnaire et son tenu : quasiment aucun beat.
        const std::vector<float> pink = PinkNoise(10.0f);
        lwe::SpectrumAnalyzer noise(kRate);
        const Result r = Run(noise, pink);
        std::snprintf(label, sizeof(label), "bruit rose stationnaire : %u beats en 10 s", r.beats);
        Check(r.beats <= 4, label);

        std::vector<float> tone(kRate * 6);
        for (size_t i = 0; i < tone.size(); ++i) {
            tone[i] = 0.3f * std::sin(kTwoPi * 220.0f * static_cast<float>(i) / kRate);
        }
        lwe::SpectrumAnalyzer steady(kRate);
        const Result t = Run(steady, tone);
        std::snprintf(label, sizeof(label), "son tenu : %u beat(s) (attaque initiale au plus)", t.beats);
        Check(t.beats <= 1, label);
    }

    {
        // Apres l'arret de la musique, le tempo est oublie et l'intensite retombe.
        lwe::SpectrumAnalyzer analyzer(kRate);
        Run(analyzer, ClickTrack(120.0f, 8.0f));
        const float bpmBefore = analyzer.Spectrum().bpm;
        for (int i = 0; i < 700; ++i) {
            analyzer.Decay(0.88f, 0.01f);  // 7 s sans aucun paquet
        }
        std::snprintf(label, sizeof(label), "arret : tempo %.1f -> %.1f, intensite %.2f", bpmBefore,
                      analyzer.Spectrum().bpm, analyzer.Spectrum().beat);
        Check(bpmBefore > 0.0f && analyzer.Spectrum().bpm == 0.0f && analyzer.Spectrum().beat < 0.01f, label);
    }

    {
        // Sensibilite : plus basse = pas plus de beats ; un kick tres faible n'est vu qu'en haute sensibilite.
        auto quiet = DrumLoop(120.0f, 10.0f);
        for (auto& s : quiet) {
            s *= 0.02f;
        }
        lwe::SpectrumAnalyzer low(kRate);
        lwe::SpectrumAnalyzer high(kRate);
        low.SetAutoGain(false);
        high.SetAutoGain(false);
        low.SetBeatSensitivity(0.25f);
        high.SetBeatSensitivity(2.0f);
        const Result l = Run(low, quiet);
        const Result h = Run(high, quiet);
        std::snprintf(label, sizeof(label), "sensibilite : %u beats a 0,25 <= %u beats a 2", l.beats, h.beats);
        Check(l.beats <= h.beats && h.beats >= 15, label);
    }
}
