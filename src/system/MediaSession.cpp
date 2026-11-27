// C++/WinRT isole dans ce fichier (en-tetes lourds, macros Windows en conflit).
#include "system/MediaSession.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "core/Log.h"

namespace lwe {
namespace {

namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

constexpr DWORD kPollIntervalMs = 1000;
constexpr uint32_t kMaxArtworkBytes = 2u * 1024u * 1024u;

std::string Base64(const std::vector<uint8_t>& bytes) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += kTable[(v >> 6) & 63];
        out += kTable[v & 63];
    }
    if (i + 1 == bytes.size()) {
        const uint32_t v = bytes[i] << 16;
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const uint32_t v = (bytes[i] << 16) | (bytes[i + 1] << 8);
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += kTable[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string ReadArtwork(const wss::IRandomAccessStreamReference& reference) {
    if (!reference) {
        return {};
    }
    const auto stream = reference.OpenReadAsync().get();
    const uint64_t size = stream.Size();
    if (size == 0 || size > kMaxArtworkBytes) {
        return {};
    }
    wss::DataReader reader(stream);
    reader.LoadAsync(static_cast<uint32_t>(size)).get();
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    reader.ReadBytes(bytes);
    std::string type = winrt::to_string(stream.ContentType());
    if (type.empty() || type.find('/') == std::string::npos) {
        type = "image/png";
    }
    return "data:" + type + ";base64," + Base64(bytes);
}

double Seconds(winrt::Windows::Foundation::TimeSpan span) {
    return std::chrono::duration<double>(span).count();
}

}  // namespace

MediaSession::MediaSession() : stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

MediaSession::~MediaSession() {
    SetEnabled(false);
}

void MediaSession::SetEnabled(bool enabled) {
    if (enabled == thread_.joinable()) {
        return;
    }
    if (enabled) {
        ResetEvent(stopEvent_.get());
        thread_ = std::thread(&MediaSession::ThreadMain, this);
        log::Info(L"Morceau en cours : surveillance demarree");
        return;
    }
    SetEvent(stopEvent_.get());
    thread_.join();
    log::Info(L"Morceau en cours : surveillance arretee");
}

uint64_t MediaSession::Snapshot(MediaInfo& out) const {
    std::scoped_lock lock(mutex_);
    out = info_;
    return serial_;
}

void MediaSession::ThreadMain() {
    SetThreadDescription(GetCurrentThread(), L"WallEnginePaper media");
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        log::Warn(L"Morceau en cours : initialisation WinRT impossible");
        return;
    }
    wmc::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    std::wstring trackKey;
    std::string artwork;
    uint64_t trackSerial = 0;
    bool loggedError = false;

    do {
        MediaInfo next;
        try {
            if (!manager) {
                manager = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            }
            const auto session = manager ? manager.GetCurrentSession() : nullptr;
            if (session) {
                const auto properties = session.TryGetMediaPropertiesAsync().get();
                const auto playback = session.GetPlaybackInfo();
                const auto timeline = session.GetTimelineProperties();
                next.available = true;
                next.app = std::wstring(session.SourceAppUserModelId());
                next.title = std::wstring(properties.Title());
                next.artist = std::wstring(properties.Artist());
                next.album = std::wstring(properties.AlbumTitle());
                next.playing = playback.PlaybackStatus() ==
                               wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                next.durationSeconds = Seconds(timeline.EndTime() - timeline.StartTime());
                next.positionSeconds = Seconds(timeline.Position() - timeline.StartTime());
                if (next.playing) {
                    // La position n'est mise a jour que de temps en temps par l'application : extrapolation.
                    const auto now = winrt::clock::now();
                    const double elapsed = std::chrono::duration<double>(now - timeline.LastUpdatedTime()).count();
                    if (elapsed > 0.0 && elapsed < 24 * 3600) {
                        next.positionSeconds += elapsed;
                    }
                }
                if (next.durationSeconds > 0.0) {
                    next.positionSeconds = std::min(next.positionSeconds, next.durationSeconds);
                }
                const std::wstring key = next.app + L"|" + next.title + L"|" + next.artist + L"|" + next.album;
                if (key != trackKey) {
                    trackKey = key;
                    ++trackSerial;
                    try {
                        artwork = ReadArtwork(properties.Thumbnail());
                    } catch (...) {
                        artwork.clear();  // pochette illisible : le reste reste valable
                    }
                    // Ni titre ni artiste dans le journal (vie privee) : seulement la presence d'une pochette.
                    log::Info(L"Morceau en cours : nouveau morceau ({}, titre {} car., artiste {} car., pochette {} Ko)",
                              next.playing ? L"lecture" : L"pause", next.title.size(), next.artist.size(),
                              artwork.size() * 3 / 4 / 1024);
                }
                next.artworkDataUrl = artwork;
                next.trackSerial = trackSerial;
            }
            loggedError = false;
        } catch (const winrt::hresult_error& e) {
            if (!loggedError) {
                log::Warn(L"Morceau en cours : {}", std::wstring(e.message()));
                loggedError = true;
            }
            manager = nullptr;
        } catch (...) {
            manager = nullptr;
        }

        {
            std::scoped_lock lock(mutex_);
            const bool changed = next.available != info_.available || next.title != info_.title ||
                                 next.artist != info_.artist || next.album != info_.album ||
                                 next.playing != info_.playing || next.trackSerial != info_.trackSerial ||
                                 std::abs(next.positionSeconds - info_.positionSeconds) > 0.5 ||
                                 next.durationSeconds != info_.durationSeconds;
            if (changed) {
                info_ = std::move(next);
                ++serial_;
            }
        }
    } while (WaitForSingleObject(stopEvent_.get(), kPollIntervalMs) == WAIT_TIMEOUT);

    manager = nullptr;
    winrt::uninit_apartment();
}

}  // namespace lwe
