// Tests de la phase 3 : lever / coucher du soleil (NOAA), planification, raccourcis, profils.
#include <cmath>
#include <cstdio>
#include <string>

#include "TestUtil.h"
#include "control/Profiles.h"
#include "control/Scheduler.h"
#include "control/SunTimes.h"

using lwe::test::Check;

namespace {

void CheckSun(const char* city, int y, int m, int d, double lat, double lon, double tz, const char* sunrise,
              const char* sunset) {
    const lwe::SunTimes sun = lwe::ComputeSunTimes(y, m, d, lat, lon, tz);
    const auto parse = [](const char* hhmm) { return std::stoi(std::string(hhmm, 2)) * 60.0 + std::stoi(std::string(hhmm + 3, 2)); };
    char label[200];
    std::snprintf(label, sizeof(label), "%s %04d-%02d-%02d : lever %s (attendu %s), coucher %s (attendu %s)", city, y, m,
                  d, lwe::FormatMinutes(sun.sunriseMinutes).c_str(), sunrise,
                  lwe::FormatMinutes(sun.sunsetMinutes).c_str(), sunset);
    Check(sun.kind == lwe::SunTimes::Kind::Normal && std::fabs(sun.sunriseMinutes - parse(sunrise)) <= 4.0 &&
              std::fabs(sun.sunsetMinutes - parse(sunset)) <= 4.0,
          label);
}

lwe::WallpaperRef Ref(const wchar_t* name) {
    return lwe::WallpaperRef{lwe::WallpaperType::Shader, name};
}

}  // namespace

void RunControlTests() {
    lwe::test::Section("Soleil (NOAA)");
    // References : calculateur solaire du NOAA (a la minute pres).
    CheckSun("Paris", 2026, 6, 21, 48.8566, 2.3522, 2.0, "05:47", "21:58");
    CheckSun("Paris", 2026, 12, 21, 48.8566, 2.3522, 1.0, "08:42", "16:56");
    CheckSun("New York", 2026, 3, 20, 40.7128, -74.0060, -4.0, "06:59", "19:08");
    CheckSun("Sydney", 2026, 12, 21, -33.8688, 151.2093, 11.0, "05:41", "20:05");
    {
        const auto summer = lwe::ComputeSunTimes(2026, 6, 21, 69.6492, 18.9553, 2.0);
        const auto winter = lwe::ComputeSunTimes(2026, 12, 21, 69.6492, 18.9553, 1.0);
        Check(summer.kind == lwe::SunTimes::Kind::PolarDay && winter.kind == lwe::SunTimes::Kind::PolarNight,
              "Tromso : soleil de minuit en juin, nuit polaire en decembre");
    }

    lwe::test::Section("Planification");
    {
        lwe::ScheduleSettings schedule;
        schedule.mode = lwe::ScheduleMode::TimeSlots;
        schedule.slots = {lwe::TimeSlot{7 * 60, 19 * 60, Ref(L"jour.hlsl")}, lwe::TimeSlot{22 * 60, 6 * 60, Ref(L"nuit.hlsl")}};
        lwe::ScheduleContext context;
        context.minutesOfDay = 8 * 60;
        const auto morning = lwe::EvaluateSchedule(schedule, context);
        context.minutesOfDay = 23 * 60 + 30;
        const auto lateNight = lwe::EvaluateSchedule(schedule, context);
        context.minutesOfDay = 3 * 60;
        const auto earlyNight = lwe::EvaluateSchedule(schedule, context);
        context.minutesOfDay = 20 * 60;
        const auto gap = lwe::EvaluateSchedule(schedule, context);
        context.minutesOfDay = 19 * 60;
        const auto atEnd = lwe::EvaluateSchedule(schedule, context);
        Check(morning.wallpaper == Ref(L"jour.hlsl") && lateNight.wallpaper == Ref(L"nuit.hlsl") &&
                  earlyNight.wallpaper == Ref(L"nuit.hlsl") && gap.wallpaper.Empty() && atEnd.wallpaper.Empty(),
              "plages horaires : 7h-19h, 22h-6h (passe minuit), trou 19h-22h, fin exclue");
    }
    {
        lwe::ScheduleSettings schedule;
        schedule.mode = lwe::ScheduleMode::Sun;
        schedule.dayWallpaper = Ref(L"jour.hlsl");
        schedule.nightWallpaper = Ref(L"nuit.hlsl");
        lwe::ScheduleContext context{2026, 6, 21, 12 * 60, 2.0, false};
        const auto noon = lwe::EvaluateSchedule(schedule, context);
        context.minutesOfDay = 23 * 60;
        const auto night = lwe::EvaluateSchedule(schedule, context);
        context.minutesOfDay = 5 * 60 + 30;  // avant le lever (05:47)
        const auto beforeSunrise = lwe::EvaluateSchedule(schedule, context);
        schedule.sunriseOffsetMinutes = -30;  // jour 30 min avant le lever
        const auto withOffset = lwe::EvaluateSchedule(schedule, context);
        Check(noon.wallpaper == schedule.dayWallpaper && night.wallpaper == schedule.nightWallpaper &&
                  beforeSunrise.wallpaper == schedule.nightWallpaper && withOffset.wallpaper == schedule.dayWallpaper,
              "soleil : jour / nuit a Paris, decalage du lever");
    }
    {
        lwe::ScheduleSettings schedule;
        schedule.mode = lwe::ScheduleMode::Theme;
        schedule.lightWallpaper = Ref(L"clair.hlsl");
        schedule.darkWallpaper = Ref(L"sombre.hlsl");
        lwe::ScheduleContext context;
        context.lightTheme = true;
        const auto light = lwe::EvaluateSchedule(schedule, context);
        context.lightTheme = false;
        const auto dark = lwe::EvaluateSchedule(schedule, context);
        Check(light.wallpaper == schedule.lightWallpaper && dark.wallpaper == schedule.darkWallpaper,
              "theme Windows : clair / sombre");
    }

    lwe::test::Section("Raccourcis");
    {
        const auto defaults = lwe::HotkeySettings::DefaultHotkeys();
        bool roundTrip = true;
        for (const auto& hotkey : defaults) {
            roundTrip = roundTrip && lwe::ParseHotkey(lwe::FormatHotkey(hotkey)) == hotkey;
        }
        const auto f5 = lwe::ParseHotkey("Ctrl+Shift+F5");
        const auto num = lwe::ParseHotkey("Win+Num7");
        Check(roundTrip && lwe::FormatHotkey(defaults[0]) == "Ctrl+Alt+Right" &&
                  lwe::FormatHotkey(defaults[0], true) == "Ctrl + Alt + Flèche droite" && f5 && f5->modifiers == 0x6 &&
                  f5->key == 0x74 && num && num->modifiers == 0x8 && num->key == 0x67,
              "format et lecture des raccourcis (aller-retour, F5, pave numerique)");
        Check(!lwe::ParseHotkey("") && !lwe::ParseHotkey("Ctrl+Alt") && !lwe::ParseHotkey("Ctrl+Bidule") &&
                  !lwe::ParseHotkey("Q+Ctrl"),
              "raccourcis invalides refuses");
    }

    lwe::test::Section("Profils");
    {
        Check(lwe::ExecutableMatches("game.exe", L"C:\\Jeux\\Game.EXE") && lwe::ExecutableMatches("Game", L"D:/x/game.exe") &&
                  !lwe::ExecutableMatches("game.exe", L"C:\\Jeux\\gamelauncher.exe") && !lwe::ExecutableMatches("", L"a.exe"),
              "correspondance d'executable (casse, chemin, extension facultative)");

        lwe::ProfileSettings settings;
        settings.active = "Travail";
        settings.rules = {lwe::ProfileRule{"game.exe", "Gaming"}, lwe::ProfileRule{"orphelin.exe", "Inexistant"}};
        const auto inGame = lwe::ResolveProfileName(settings, L"C:\\game.exe", true);
        const auto onBattery = lwe::ResolveProfileName(settings, L"C:\\notepad.exe", true);
        const auto manual = lwe::ResolveProfileName(settings, L"C:\\notepad.exe", false);
        const auto orphan = lwe::ResolveProfileName(settings, L"C:\\orphelin.exe", false);
        settings.autoSwitch = false;
        const auto noAuto = lwe::ResolveProfileName(settings, L"C:\\game.exe", false);
        Check(inGame == "Gaming" && onBattery == "Batterie" && manual == "Travail" && orphan == "Travail" &&
                  noAuto == "Travail",
              "priorite : regle d'executable > batterie > choix manuel ; regle orpheline ignoree");

        lwe::AppConfig config;
        config.fpsLimit = 144;
        config.audio.volume = 0.8f;
        const auto base = lwe::ComputeEffectiveSettings(config, nullptr, false);
        const auto gaming = lwe::ComputeEffectiveSettings(config, lwe::FindProfile(settings, "Gaming"), false);
        const auto reduced = lwe::ComputeEffectiveSettings(config, nullptr, true);
        Check(base.fpsLimit == 144 && base.volume == 0.8f && gaming.fpsLimit == 30 && gaming.renderScale == 0.75f &&
                  gaming.muted && gaming.pause.onMaximizedApp && reduced.fpsLimit == lwe::kReducedMotionFps &&
                  reduced.reducedMotion,
              "reglages effectifs : base, profil Gaming, animations reduites");
    }
}
