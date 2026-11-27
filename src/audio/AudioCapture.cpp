#include "audio/AudioCapture.h"

#include <mmreg.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "audio/SpectrumAnalyzer.h"
#include "core/Log.h"

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kBufferDuration = 2'000'000;  // 200 ms de tampon partage
constexpr DWORD kPollIntervalMs = 10;
constexpr DWORD kRetryDelayMs = 2000;
constexpr ULONGLONG kDeviceCheckIntervalMs = 2000;
constexpr float kSilenceDecay = 0.88f;  // par tour de boucle sans donnees (10 ms)

// PKEY_Device_FriendlyName (functiondiscoverykeys_devpkey.h), redefini pour eviter initguid.h.
constexpr PROPERTYKEY kDeviceFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

enum class SampleKind { Float32, Int16, Int32, Unsupported };

SampleKind DetectKind(const WAVEFORMATEX* format) {
    GUID subtype{};
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
        subtype = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat;
    } else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        subtype = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    } else if (format->wFormatTag == WAVE_FORMAT_PCM) {
        subtype = KSDATAFORMAT_SUBTYPE_PCM;
    }
    if (subtype == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && format->wBitsPerSample == 32) {
        return SampleKind::Float32;
    }
    if (subtype == KSDATAFORMAT_SUBTYPE_PCM && format->wBitsPerSample == 16) {
        return SampleKind::Int16;
    }
    if (subtype == KSDATAFORMAT_SUBTYPE_PCM && format->wBitsPerSample == 32) {
        return SampleKind::Int32;
    }
    return SampleKind::Unsupported;
}

std::wstring DeviceId(IMMDevice* device) {
    LPWSTR id = nullptr;
    std::wstring result;
    if (device && SUCCEEDED(device->GetId(&id)) && id) {
        result = id;
    }
    CoTaskMemFree(id);
    return result;
}

std::wstring FriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    std::wstring name;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(kDeviceFriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
            name = value.pwszVal;
        }
        PropVariantClear(&value);
    }
    return name;
}

// Session de capture ouverte sur un peripherique.
struct CaptureSession {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    std::wstring deviceId;
    std::wstring deviceName;
    UINT32 channels = 2;
    SampleKind kind = SampleKind::Unsupported;
    std::unique_ptr<SpectrumAnalyzer> analyzer;

    ~CaptureSession() {
        if (client) {
            client->Stop();
        }
    }
};

std::unique_ptr<CaptureSession> OpenDefaultOutput(IMMDeviceEnumerator* enumerator, std::wstring& error) {
    auto session = std::make_unique<CaptureSession>();
    HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &session->device);
    if (FAILED(hr)) {
        error = L"aucune sortie audio";
        return nullptr;
    }
    hr = session->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(session->client.GetAddressOf()));
    WAVEFORMATEX* format = nullptr;
    if (SUCCEEDED(hr)) {
        hr = session->client->GetMixFormat(&format);
    }
    if (FAILED(hr) || !format) {
        error = L"format de mixage indisponible : " + HResultToString(hr);
        return nullptr;
    }
    session->kind = DetectKind(format);
    session->channels = std::max<UINT32>(1, format->nChannels);
    const uint32_t sampleRate = format->nSamplesPerSec;
    if (session->kind != SampleKind::Unsupported) {
        hr = session->client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kBufferDuration, 0,
                                         format, nullptr);
    }
    CoTaskMemFree(format);
    if (session->kind == SampleKind::Unsupported) {
        error = L"format audio non pris en charge";
        return nullptr;
    }
    if (SUCCEEDED(hr)) {
        hr = session->client->GetService(IID_PPV_ARGS(&session->capture));
    }
    if (SUCCEEDED(hr)) {
        hr = session->client->Start();
    }
    if (FAILED(hr)) {
        error = L"capture loopback : " + HResultToString(hr);
        return nullptr;
    }
    session->deviceId = DeviceId(session->device.Get());
    session->deviceName = FriendlyName(session->device.Get());
    session->analyzer = std::make_unique<SpectrumAnalyzer>(sampleRate);
    log::Info(L"Audio : capture de « {} » ({} Hz, {} canaux)", session->deviceName, sampleRate,
              session->channels);
    return session;
}

}  // namespace

AudioCapture::AudioCapture() : stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

AudioCapture::~AudioCapture() {
    SetEnabled(false);
}

void AudioCapture::SetEnabled(bool enabled) {
    if (enabled == thread_.joinable()) {
        return;
    }
    if (enabled) {
        ResetEvent(stopEvent_.get());
        thread_ = std::thread(&AudioCapture::ThreadMain, this);
        return;
    }
    SetEvent(stopEvent_.get());
    thread_.join();
    std::scoped_lock lock(mutex_);
    spectrum_ = AudioSpectrum{};
}

void AudioCapture::Snapshot(AudioSpectrum& out) const {
    std::scoped_lock lock(mutex_);
    out = spectrum_;
}

std::wstring AudioCapture::DeviceName() const {
    std::scoped_lock lock(mutex_);
    return deviceName_;
}

void AudioCapture::ThreadMain() {
    SetThreadDescription(GetCurrentThread(), L"WallEnginePaper audio");
    ScopedComInit com(COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(com.Result()) ||
        FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        log::Error(L"Audio : MMDeviceEnumerator indisponible");
        return;
    }

    std::unique_ptr<CaptureSession> session;
    std::vector<float> mono;
    ULONGLONG lastDeviceCheck = GetTickCount64();
    std::wstring lastError;
    const auto publish = [&](const AudioSpectrum& spectrum) {
        std::scoped_lock lock(mutex_);
        spectrum_ = spectrum;
    };

    // Statistiques de diagnostic (--audio-debug), remises a zero chaque seconde.
    struct DebugStats {
        ULONGLONG start = 0;
        uint64_t frames = 0;
        uint64_t publishes = 0;
        float peakSample = 0.0f;
        float maxLevel = 0.0f;
        double sumLevel = 0.0;
        float maxBass = 0.0f, maxMid = 0.0f, maxTreble = 0.0f;
        size_t peakBand = 0;
        float peakBandValue = 0.0f;
    } stats;
    stats.start = GetTickCount64();
    const auto range = [](const AudioSpectrum& s, size_t first, size_t last) {
        float sum = 0.0f;
        for (size_t i = first; i <= last; ++i) {
            sum += s.bands[i];
        }
        return sum / static_cast<float>(last - first + 1);
    };
    const auto accumulate = [&](const AudioSpectrum& s, float gainDb) {
        ++stats.publishes;
        stats.maxLevel = std::max(stats.maxLevel, s.level);
        stats.sumLevel += s.level;
        stats.maxBass = std::max(stats.maxBass, range(s, 0, 7));
        stats.maxMid = std::max(stats.maxMid, range(s, 8, 31));
        stats.maxTreble = std::max(stats.maxTreble, range(s, 32, 63));
        for (size_t i = 0; i < kAudioBandCount; ++i) {
            if (s.bands[i] > stats.peakBandValue) {
                stats.peakBandValue = s.bands[i];
                stats.peakBand = i;
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (now - stats.start >= 1000) {
            log::Info(L"Audio debug : {} ech., crete {:.3f}, niveau moy {:.2f} max {:.2f}, graves {:.2f}, medium {:.2f}, "
                      L"aigus {:.2f}, bande max {} ({:.2f}), gain auto {:+.1f} dB",
                      stats.frames, stats.peakSample, stats.publishes ? stats.sumLevel / stats.publishes : 0.0,
                      stats.maxLevel, stats.maxBass, stats.maxMid, stats.maxTreble, stats.peakBand,
                      stats.peakBandValue, gainDb);
            stats = DebugStats{};
            stats.start = now;
        }
    };

    while (true) {
        if (!session) {
            std::wstring error;
            session = OpenDefaultOutput(enumerator.Get(), error);
            capturing_ = session != nullptr;
            {
                std::scoped_lock lock(mutex_);
                deviceName_ = session ? session->deviceName : std::wstring();
                if (!session) {
                    spectrum_ = AudioSpectrum{};
                }
            }
            if (!session) {
                if (error != lastError) {
                    log::Warn(L"Audio : {} (nouvel essai toutes les {} s)", error, kRetryDelayMs / 1000);
                    lastError = error;
                }
                if (WaitForSingleObject(stopEvent_.get(), kRetryDelayMs) == WAIT_OBJECT_0) {
                    break;
                }
                continue;
            }
            lastError.clear();
        }

        if (WaitForSingleObject(stopEvent_.get(), kPollIntervalMs) == WAIT_OBJECT_0) {
            break;
        }

        session->analyzer->SetAutoGain(autoGain_);
        bool received = false;
        bool failed = false;
        UINT32 packet = 0;
        while (true) {
            // Echec (AUDCLNT_E_DEVICE_INVALIDATED : peripherique desactive, format de mixage change...) :
            // la session est rouverte, meme si la sortie par defaut reste la meme.
            const HRESULT next = session->capture->GetNextPacketSize(&packet);
            if (FAILED(next)) {
                log::Warn(L"Audio : capture interrompue ({}), reouverture", HResultToString(next));
                failed = true;
                break;
            }
            if (packet == 0) {
                break;
            }
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (const HRESULT hr = session->capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr); FAILED(hr)) {
                log::Warn(L"Audio : lecture du tampon impossible ({}), reouverture", HResultToString(hr));
                failed = true;
                break;
            }
            // Conversion en mono flottant (moyenne des canaux).
            mono.resize(frames);
            const UINT32 channels = session->channels;
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data;
            for (UINT32 f = 0; f < frames; ++f) {
                float sum = 0.0f;
                if (!silent) {
                    for (UINT32 c = 0; c < channels; ++c) {
                        const size_t index = static_cast<size_t>(f) * channels + c;
                        switch (session->kind) {
                        case SampleKind::Float32: sum += reinterpret_cast<const float*>(data)[index]; break;
                        case SampleKind::Int16: sum += reinterpret_cast<const int16_t*>(data)[index] / 32768.0f; break;
                        case SampleKind::Int32: sum += reinterpret_cast<const int32_t*>(data)[index] / 2147483648.0f; break;
                        case SampleKind::Unsupported: break;
                        }
                    }
                }
                mono[f] = sum / static_cast<float>(channels);
                stats.peakSample = std::max(stats.peakSample, std::fabs(mono[f]));
            }
            stats.frames += frames;
            session->capture->ReleaseBuffer(frames);
            if (session->analyzer->Push(mono.data(), mono.size())) {
                received = true;
            }
        }
        if (failed) {
            session.reset();  // peripherique invalide (debranche, change...) : on rouvre
            continue;
        }
        // Rien ne joue : en loopback, Windows n'envoie alors aucun paquet.
        if (!received) {
            session->analyzer->Decay(kSilenceDecay);
        }
        publish(session->analyzer->Spectrum());
        if (debug_) {
            accumulate(session->analyzer->Spectrum(), session->analyzer->GainDb());
        }

        // Sortie par defaut changee (casque, autre carte son) : on suit.
        const ULONGLONG now = GetTickCount64();
        if (now - lastDeviceCheck >= kDeviceCheckIntervalMs) {
            lastDeviceCheck = now;
            ComPtr<IMMDevice> current;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &current)) &&
                DeviceId(current.Get()) != session->deviceId) {
                log::Info(L"Audio : la sortie par defaut a change");
                session.reset();
            }
        }
    }
    session.reset();
    capturing_ = false;
}

}  // namespace lwe
