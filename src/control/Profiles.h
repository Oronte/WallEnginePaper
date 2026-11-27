#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "config/AppConfig.h"

namespace lwe {

// Reglages reellement appliques : reglages de base, ecrases par le profil actif, puis par le mode
// d'animations reduites (accessibilite). Logique pure, testee.
struct EffectiveSettings {
    uint32_t fpsLimit = 60;
    float renderScale = 1.0f;
    PauseRules pause;
    float volume = 0.5f;
    bool muted = false;
    bool reducedMotion = false;  // pas de parallaxe ni de transition, cadence plafonnee
    std::string profile;         // nom du profil applique ("" = aucun)
};

inline constexpr uint32_t kReducedMotionFps = 15;

// Profil a appliquer : regle de l'executable au premier plan (si la bascule automatique est active), sinon
// profil "batterie" quand le PC est sur batterie, sinon le profil choisi a la main. "" = aucun profil.
std::string ResolveProfileName(const ProfileSettings& settings, std::wstring_view foregroundExecutable, bool onBattery);

const Profile* FindProfile(const ProfileSettings& settings, std::string_view name) noexcept;

EffectiveSettings ComputeEffectiveSettings(const AppConfig& config, const Profile* profile, bool reducedMotion);

// "C:\\Jeux\\Game.EXE" -> "game.exe" ; une regle "game" (sans extension) correspond aussi.
bool ExecutableMatches(std::string_view rule, std::wstring_view executablePath);

}  // namespace lwe
