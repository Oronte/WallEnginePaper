#pragma once

namespace lwe {

// Lever et coucher du soleil (algorithme du NOAA Solar Calculator), calcules localement a partir d'une
// latitude / longitude saisies a la main : aucune requete reseau. Precision ~1 minute aux latitudes moyennes.
struct SunTimes {
    enum class Kind { Normal, PolarDay, PolarNight };
    Kind kind = Kind::Normal;
    double sunriseMinutes = 0.0;  // minutes depuis minuit, heure locale (peut sortir de [0, 1440[ en bord de zone)
    double sunsetMinutes = 0.0;
};

// `latitude` en degres (nord positif), `longitude` en degres (est positif), `utcOffsetHours` decalage de l'heure
// locale pour cette date (ete compris, ex. +2 a Paris en juin).
SunTimes ComputeSunTimes(int year, int month, int day, double latitude, double longitude, double utcOffsetHours);

}  // namespace lwe
