#include "control/Profiles.h"

#include <algorithm>

#include "core/StringUtil.h"

namespace lwe {

bool ExecutableMatches(std::string_view rule, std::wstring_view executablePath) {
    if (rule.empty() || executablePath.empty()) {
        return false;
    }
    std::wstring name(executablePath);
    if (const size_t slash = name.find_last_of(L"\\/"); slash != std::wstring::npos) {
        name.erase(0, slash + 1);
    }
    name = ToLower(name);
    std::wstring wanted = ToLower(Utf8ToWide(rule));
    if (const size_t slash = wanted.find_last_of(L"\\/"); slash != std::wstring::npos) {
        wanted.erase(0, slash + 1);
    }
    if (name == wanted) {
        return true;
    }
    return !wanted.ends_with(L".exe") && name == wanted + L".exe";
}

const Profile* FindProfile(const ProfileSettings& settings, std::string_view name) noexcept {
    if (name.empty()) {
        return nullptr;
    }
    const auto it = std::find_if(settings.profiles.begin(), settings.profiles.end(),
                                 [&](const Profile& p) { return p.name == name; });
    return it == settings.profiles.end() ? nullptr : &*it;
}

std::string ResolveProfileName(const ProfileSettings& settings, std::wstring_view foregroundExecutable, bool onBattery) {
    if (settings.autoSwitch) {
        for (const auto& rule : settings.rules) {
            if (ExecutableMatches(rule.executable, foregroundExecutable) && FindProfile(settings, rule.profile)) {
                return rule.profile;
            }
        }
    }
    if (onBattery && FindProfile(settings, settings.batteryProfile)) {
        return settings.batteryProfile;
    }
    return FindProfile(settings, settings.active) ? settings.active : std::string();
}

EffectiveSettings ComputeEffectiveSettings(const AppConfig& config, const Profile* profile, bool reducedMotion) {
    EffectiveSettings effective;
    effective.fpsLimit = config.fpsLimit;
    effective.renderScale = config.renderScale;
    effective.pause = config.pause;
    effective.volume = config.audio.volume;
    effective.muted = config.audio.muted;
    if (profile) {
        effective.profile = profile->name;
        effective.fpsLimit = profile->fpsLimit;
        effective.renderScale = profile->renderScale;
        effective.pause.onFullscreenApp = profile->pauseOnFullscreen;
        effective.pause.onMaximizedApp = profile->pauseOnMaximized;
        effective.pause.onBattery = profile->pauseOnBattery;
        effective.volume = profile->volume;
        effective.muted = profile->muted || config.audio.muted;  // la sourdine manuelle l'emporte toujours
    }
    effective.reducedMotion = reducedMotion;
    if (reducedMotion) {
        effective.fpsLimit = std::min(effective.fpsLimit, kReducedMotionFps);
    }
    return effective;
}

}  // namespace lwe
