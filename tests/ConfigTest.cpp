// Tests de la configuration : aller-retour JSON des nouveaux reglages et compatibilite avec un ancien
// config.json (reglages absents -> valeurs par defaut, valeurs aberrantes -> bornees).
#include <cstdio>
#include <string>

#include "TestUtil.h"
#include "config/AppConfig.h"

using lwe::test::Check;

void RunConfigTests() {
    lwe::test::Section("Configuration");

    {
        lwe::AppConfig config;
        config.transition = {lwe::TransitionType::Wipe, 2.5f};
        config.playlist.customTransition = true;
        config.playlist.transition = {lwe::TransitionType::Random, 0.0f};
        config.audio.beatSensitivity = 1.5f;
        config.audio.attackMs = 25.0f;
        config.audio.releaseMs = 400.0f;
        config.audio.autoGain = false;

        lwe::AppConfig loaded;
        std::string error;
        const bool ok = lwe::DeserializeConfig(lwe::SerializeConfig(config), loaded, error);
        Check(ok && loaded.transition == config.transition && loaded.playlist.customTransition &&
                  loaded.playlist.transition == config.playlist.transition,
              "aller-retour JSON : transitions globale et de playlist");
        Check(loaded.audio.beatSensitivity == 1.5f && loaded.audio.attackMs == 25.0f && loaded.audio.releaseMs == 400.0f &&
                  !loaded.audio.autoGain,
              "aller-retour JSON : reglages audio (beat, lissage, gain automatique)");
    }

    {
        // config.json de la v0.3 : aucun des nouveaux reglages.
        const char* legacy = R"({"version": 2, "fpsLimit": 30, "audio": {"volume": 0.4, "muted": false},
                                 "playlist": {"enabled": true, "intervalMinutes": 5}})";
        lwe::AppConfig loaded;
        std::string error;
        const bool ok = lwe::DeserializeConfig(legacy, loaded, error);
        const lwe::AppConfig defaults;
        Check(ok && loaded.fpsLimit == 30 && loaded.playlist.enabled && loaded.transition == defaults.transition &&
                  !loaded.playlist.customTransition && loaded.audio.autoGain &&
                  loaded.audio.beatSensitivity == defaults.audio.beatSensitivity,
              "ancien config.json : nouveaux reglages a leur valeur par defaut");
    }

    {
        const char* broken = R"({"transition": {"type": "inconnu", "seconds": 99},
                                 "audio": {"beatSensitivity": -3, "releaseMs": 1e9}})";
        lwe::AppConfig loaded;
        std::string error;
        lwe::DeserializeConfig(broken, loaded, error);
        Check(loaded.transition.type == lwe::TransitionType::Fade && loaded.transition.seconds == 3.0f &&
                  loaded.audio.beatSensitivity == 0.25f && loaded.audio.releaseMs == 3000.0f,
              "valeurs aberrantes bornees, type inconnu ignore");
    }

    {
        bool allConcrete = true;
        bool seen[4] = {};
        for (int i = 0; i < 100; ++i) {
            const auto type = lwe::ResolveTransitionType(lwe::TransitionType::Random, static_cast<float>(i) / 100.0f);
            allConcrete = allConcrete && type != lwe::TransitionType::Random;
            seen[static_cast<int>(type) % 4] = true;
        }
        Check(allConcrete && seen[0] && seen[1] && seen[2] && seen[3] &&
                  lwe::ResolveTransitionType(lwe::TransitionType::Wipe, 0.9f) == lwe::TransitionType::Wipe,
              "transition aleatoire : toujours un type concret, les quatre possibles");
    }
}
