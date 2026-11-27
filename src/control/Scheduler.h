#pragma once

#include <optional>
#include <string>

#include "config/AppConfig.h"
#include "control/SunTimes.h"

namespace lwe {

// Moment et contexte d'evaluation de la planification (fournis par l'application ; logique pure, testee).
struct ScheduleContext {
    int year = 2026;
    int month = 1;
    int day = 1;
    double minutesOfDay = 0.0;     // heure locale, minutes depuis minuit
    double utcOffsetHours = 0.0;   // decalage local du jour (ete compris)
    bool lightTheme = false;       // theme des applications Windows
};

struct ScheduleDecision {
    WallpaperRef wallpaper;   // vide : rien a imposer (aucune plage ne couvre ce moment)
    std::string reason;       // "plage 07:00-19:00", "jour", "nuit", "theme clair"...
};

// Fond impose par la planification a ce moment, selon le mode choisi.
ScheduleDecision EvaluateSchedule(const ScheduleSettings& schedule, const ScheduleContext& context);

// Plage horaire contenant ce moment (fin exclue ; une plage dont la fin precede le debut passe minuit).
bool SlotContains(const TimeSlot& slot, double minutesOfDay) noexcept;

// "07:30" ; "24:00" pour la fin de journee.
std::string FormatMinutes(double minutes);

}  // namespace lwe
