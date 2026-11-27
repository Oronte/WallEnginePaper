#include "control/SunTimes.h"

#include <cmath>
#include <numbers>

namespace lwe {
namespace {

constexpr double kDeg = std::numbers::pi / 180.0;

// Jour julien a 0 h TU (calendrier gregorien).
double JulianDay(int year, int month, int day) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    const int a = year / 100;
    const int b = 2 - a + a / 4;
    return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day + b - 1524.5;
}

double Wrap360(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

}  // namespace

SunTimes ComputeSunTimes(int year, int month, int day, double latitude, double longitude, double utcOffsetHours) {
    // Position du soleil a midi local (formules du tableur NOAA).
    const double jd = JulianDay(year, month, day) + 0.5 - utcOffsetHours / 24.0;
    const double t = (jd - 2451545.0) / 36525.0;  // siecles juliens depuis J2000

    const double meanLongitude = Wrap360(280.46646 + t * (36000.76983 + t * 0.0003032));
    const double meanAnomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    const double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    const double center = std::sin(meanAnomaly * kDeg) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                          std::sin(2.0 * meanAnomaly * kDeg) * (0.019993 - 0.000101 * t) +
                          std::sin(3.0 * meanAnomaly * kDeg) * 0.000289;
    const double trueLongitude = meanLongitude + center;
    const double omega = 125.04 - 1934.136 * t;
    const double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * std::sin(omega * kDeg);
    const double meanObliquity = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
    const double obliquity = meanObliquity + 0.00256 * std::cos(omega * kDeg);
    const double declination = std::asin(std::sin(obliquity * kDeg) * std::sin(apparentLongitude * kDeg));

    const double y = std::pow(std::tan(obliquity * kDeg / 2.0), 2.0);
    const double l0 = meanLongitude * kDeg;
    const double m = meanAnomaly * kDeg;
    const double equationOfTime =
        4.0 / kDeg *
        (y * std::sin(2.0 * l0) - 2.0 * eccentricity * std::sin(m) +
         4.0 * eccentricity * y * std::sin(m) * std::cos(2.0 * l0) - 0.5 * y * y * std::sin(4.0 * l0) -
         1.25 * eccentricity * eccentricity * std::sin(2.0 * m));

    const double solarNoon = 720.0 - 4.0 * longitude - equationOfTime + utcOffsetHours * 60.0;

    // Angle horaire du lever : soleil a -0,833 deg (refraction + demi-diametre).
    const double phi = latitude * kDeg;
    const double cosHourAngle =
        std::cos(90.833 * kDeg) / (std::cos(phi) * std::cos(declination)) - std::tan(phi) * std::tan(declination);
    SunTimes result;
    if (cosHourAngle > 1.0) {
        result.kind = SunTimes::Kind::PolarNight;
        result.sunriseMinutes = result.sunsetMinutes = solarNoon;
        return result;
    }
    if (cosHourAngle < -1.0) {
        result.kind = SunTimes::Kind::PolarDay;
        result.sunriseMinutes = 0.0;
        result.sunsetMinutes = 1440.0;
        return result;
    }
    const double hourAngle = std::acos(cosHourAngle) / kDeg;
    result.sunriseMinutes = solarNoon - 4.0 * hourAngle;
    result.sunsetMinutes = solarNoon + 4.0 * hourAngle;
    return result;
}

}  // namespace lwe
