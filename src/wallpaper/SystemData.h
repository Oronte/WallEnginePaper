#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace lwe {

// Donnees systeme exposees aux fonds (web) : morceau en cours, statistiques, theme Windows.
// Collectees localement, uniquement quand une page s'y abonne (wep.on('media' | 'system')).

enum SystemTopic : uint32_t {
    kTopicMedia = 1u << 0,
    kTopicSystem = 1u << 1,
};

struct MediaInfo {
    bool available = false;  // une application expose une session multimedia
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring app;       // identifiant de l'application (ex. Spotify.exe)
    bool playing = false;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    std::string artworkDataUrl;  // "data:image/png;base64,..." ("" si aucune pochette)
    uint64_t trackSerial = 0;    // change avec le morceau (la pochette n'est renvoyee qu'a ce moment)
};

struct SystemStatsInfo {
    float cpuPercent = 0.0f;
    float ramPercent = 0.0f;
    double ramUsedGb = 0.0;
    double ramTotalGb = 0.0;
    float gpuPercent = -1.0f;  // -1 : indisponible (compteurs PDH absents)
    double vramUsedGb = -1.0;  // memoire video dediee utilisee (tout le systeme), -1 si inconnue
    double vramTotalGb = 0.0;
    int batteryPercent = -1;   // -1 : pas de batterie
    bool charging = false;
};

struct ThemeInfo {
    bool dark = true;
    std::array<float, 4> accent{0.55f, 0.38f, 1.0f, 1.0f};
};

// Instantane pousse aux backends une fois par seconde ; les numeros de serie evitent les envois inutiles.
struct SystemSnapshot {
    const MediaInfo* media = nullptr;
    uint64_t mediaSerial = 0;
    const SystemStatsInfo* stats = nullptr;
    uint64_t statsSerial = 0;
    ThemeInfo theme;
    uint64_t themeSerial = 0;
};

}  // namespace lwe
