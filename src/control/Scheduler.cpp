#include "control/Scheduler.h"

#include <cmath>
#include <format>

namespace lwe {

bool SlotContains(const TimeSlot& slot, double minutesOfDay) noexcept {
    const double start = static_cast<double>(slot.startMinutes);
    const double end = static_cast<double>(slot.endMinutes);
    if (start == end) {
        return true;  // plage de 24 h
    }
    if (start < end) {
        return minutesOfDay >= start && minutesOfDay < end;
    }
    return minutesOfDay >= start || minutesOfDay < end;  // passe minuit (ex. 22:00-06:00)
}

std::string FormatMinutes(double minutes) {
    const int total = static_cast<int>(std::lround(minutes));
    const int wrapped = ((total % 1440) + 1440) % 1440;
    if (total == 1440) {
        return "24:00";
    }
    return std::format("{:02}:{:02}", wrapped / 60, wrapped % 60);
}

ScheduleDecision EvaluateSchedule(const ScheduleSettings& schedule, const ScheduleContext& context) {
    ScheduleDecision decision;
    switch (schedule.mode) {
    case ScheduleMode::TimeSlots:
        for (const auto& slot : schedule.slots) {
            if (!slot.wallpaper.Empty() && SlotContains(slot, context.minutesOfDay)) {
                decision.wallpaper = slot.wallpaper;
                decision.reason = "plage " + FormatMinutes(slot.startMinutes) + "-" + FormatMinutes(slot.endMinutes);
                break;
            }
        }
        break;

    case ScheduleMode::Sun: {
        const SunTimes sun = ComputeSunTimes(context.year, context.month, context.day, schedule.latitude,
                                             schedule.longitude, context.utcOffsetHours);
        bool day = false;
        switch (sun.kind) {
        case SunTimes::Kind::PolarDay: day = true; break;
        case SunTimes::Kind::PolarNight: day = false; break;
        case SunTimes::Kind::Normal: {
            const double sunrise = sun.sunriseMinutes + schedule.sunriseOffsetMinutes;
            const double sunset = sun.sunsetMinutes + schedule.sunsetOffsetMinutes;
            day = context.minutesOfDay >= sunrise && context.minutesOfDay < sunset;
            break;
        }
        }
        decision.wallpaper = day ? schedule.dayWallpaper : schedule.nightWallpaper;
        decision.reason = day ? "jour" : "nuit";
        break;
    }

    case ScheduleMode::Theme:
        decision.wallpaper = context.lightTheme ? schedule.lightWallpaper : schedule.darkWallpaper;
        decision.reason = context.lightTheme ? "thème clair" : "thème sombre";
        break;
    }
    return decision;
}

}  // namespace lwe
