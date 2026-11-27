// Tests de la phase 5 : reglages d'effets (activite, animation, JSON) et interaction souris (ondulations, trainee).
#include <cstdio>
#include <string>

#include "TestUtil.h"
#include "config/AppConfig.h"
#include "effects/Interaction.h"

using lwe::test::Check;

void RunEffectsTests() {
    lwe::test::Section("Effets");
    {
        lwe::EffectSettings none;
        lwe::EffectSettings vignette;
        vignette.strength[static_cast<size_t>(lwe::EffectId::Vignette)] = 0.5f;
        lwe::EffectSettings grain;
        grain.strength[static_cast<size_t>(lwe::EffectId::Grain)] = 0.3f;
        lwe::EffectSettings snow;
        snow.overlay = lwe::OverlayType::Snow;
        lwe::EffectSettings ripples;
        ripples.ripples = true;
        Check(!none.Active() && vignette.Active() && !vignette.Animated() && grain.Animated() && snow.Animated() &&
                  ripples.Active() && !ripples.Animated(),
              "activite : rien par defaut, vignettage statique, grain et particules animes, ondulations a la demande");
    }
    {
        lwe::AppConfig config;
        lwe::EffectSettings e;
        e.strength = {0.2f, 0.0f, 0.7f, 0.5f, 0.0f, 1.0f};
        e.order = {lwe::EffectId::Scanlines, lwe::EffectId::Vignette, lwe::EffectId::Bloom,
                   lwe::EffectId::Grain,     lwe::EffectId::Blur,     lwe::EffectId::Chromatic};
        e.overlay = lwe::OverlayType::Fireflies;
        e.overlayIntensity = 0.8f;
        e.ripples = true;
        e.trail = true;
        e.trailColor = {1.0f, 0.5f, 0.25f, 1.0f};
        config.effects["assets/shaders/aurora.hlsl"] = e;
        lwe::AppConfig loaded;
        std::string error;
        const bool ok = lwe::DeserializeConfig(lwe::SerializeConfig(config), loaded, error);
        Check(ok && loaded.effects.size() == 1 && loaded.effects.begin()->second == e,
              "aller-retour JSON des effets (forces, ordre, particules, interaction)");

        const char* broken = R"({"effects": {"x": {"strength": {"vignette": 7, "grain": -1},
                                                    "order": ["vignette", "vignette", "blur"], "overlay": "volcan"}}})";
        lwe::AppConfig repaired;
        lwe::DeserializeConfig(broken, repaired, error);
        const lwe::EffectSettings& r = repaired.effects["x"];
        Check(r.Strength(lwe::EffectId::Vignette) == 1.0f && r.Strength(lwe::EffectId::Grain) == 0.0f &&
                  r.order == lwe::EffectSettings{}.order && r.overlay == lwe::OverlayType::None,
              "valeurs bornees, ordre incomplet ou en double remplace par l'ordre par defaut");
    }

    lwe::test::Section("Interaction souris");
    {
        lwe::RippleSet set;
        for (int i = 0; i < 10; ++i) {
            set.Add(static_cast<float>(i), 0.0f);
            set.Advance(0.01f);
        }
        const bool capped = set.Items().size() == lwe::RippleSet::kMaxRipples && set.Items().front().x == 2.0f;
        set.Advance(lwe::RippleSet::kLifetime);
        Check(capped && set.Empty(), "ondulations : 8 au plus (les plus anciennes cedent), disparition apres 1,6 s");
    }
    {
        lwe::CursorTrail trail;
        trail.Update(100.0f, 100.0f, true, 0.016f);
        trail.Update(101.0f, 100.0f, true, 0.016f);  // trop proche : ignore
        trail.Update(120.0f, 100.0f, true, 0.016f);
        const bool spaced = trail.Points().size() == 2 && trail.Points().front().x == 120.0f;
        for (int i = 0; i < 40; ++i) {
            trail.Update(120.0f + static_cast<float>(i) * 10.0f, 100.0f, true, 0.001f);
        }
        const bool capped = trail.Points().size() == lwe::CursorTrail::kMaxPoints;
        trail.Update(0.0f, 0.0f, false, lwe::CursorTrail::kLifetime);
        Check(spaced && capped && trail.Empty(),
              "trainee : espacement minimal, 24 points au plus, effacee quand le curseur s'arrete ou sort");
    }
}
