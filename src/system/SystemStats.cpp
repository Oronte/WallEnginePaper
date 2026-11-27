#include "system/SystemStats.h"

#include <dxgi.h>
#include <pdhmsg.h>

#include <algorithm>
#include <vector>

#include "core/Log.h"

namespace lwe {
namespace {

uint64_t ToUInt(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// Somme des instances d'un compteur generique (ex. tous les moteurs 3D de toutes les applications).
bool SumCounter(PDH_HCOUNTER counter, double& sum) {
    DWORD bufferSize = 0;
    DWORD count = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &count, nullptr);
    if (status != PDH_MORE_DATA || bufferSize == 0) {
        return false;
    }
    std::vector<uint8_t> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &count, items);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    sum = 0.0;
    for (DWORD i = 0; i < count; ++i) {
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
            sum += items[i].FmtValue.doubleValue;
        }
    }
    return true;
}

}  // namespace

void SystemStats::Start(IDXGIAdapter* adapter) {
    if (running_) {
        return;
    }
    running_ = true;
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        lastIdle_ = ToUInt(idle);
        lastTotal_ = ToUInt(kernel) + ToUInt(user);
    }
    if (adapter) {
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc))) {
            vramTotalGb_ = static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
        }
    }
    if (PdhOpenQueryW(nullptr, 0, &query_) == ERROR_SUCCESS) {
        // Compteurs en anglais : independants de la langue de Windows.
        if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpuCounter_) !=
            ERROR_SUCCESS) {
            gpuCounter_ = nullptr;
        }
        if (PdhAddEnglishCounterW(query_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &vramCounter_) !=
            ERROR_SUCCESS) {
            vramCounter_ = nullptr;
        }
        PdhCollectQueryData(query_);  // premier echantillon (les taux ont besoin de deux mesures)
    } else {
        query_ = nullptr;
    }
    log::Info(L"Statistiques systeme : demarrees (GPU {})", gpuCounter_ ? L"disponible" : L"indisponible");
}

void SystemStats::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (query_) {
        PdhCloseQuery(query_);  // libere aussi les compteurs
    }
    query_ = nullptr;
    gpuCounter_ = vramCounter_ = nullptr;
    log::Info(L"Statistiques systeme : arretees");
}

void SystemStats::Sample(SystemStatsInfo& out) {
    out = SystemStatsInfo{};
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const uint64_t idleNow = ToUInt(idle);
        const uint64_t totalNow = ToUInt(kernel) + ToUInt(user);  // le temps noyau inclut le temps d'inactivite
        const uint64_t total = totalNow - lastTotal_;
        const uint64_t idleDelta = idleNow - lastIdle_;
        if (total > 0) {
            out.cpuPercent = std::clamp(100.0f * (1.0f - static_cast<float>(idleDelta) / static_cast<float>(total)),
                                        0.0f, 100.0f);
        }
        lastIdle_ = idleNow;
        lastTotal_ = totalNow;
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        constexpr double kGb = 1024.0 * 1024.0 * 1024.0;
        out.ramPercent = static_cast<float>(memory.dwMemoryLoad);
        out.ramTotalGb = static_cast<double>(memory.ullTotalPhys) / kGb;
        out.ramUsedGb = static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys) / kGb;
    }

    if (query_ && PdhCollectQueryData(query_) == ERROR_SUCCESS) {
        double gpu = 0.0;
        if (gpuCounter_ && SumCounter(gpuCounter_, gpu)) {
            out.gpuPercent = static_cast<float>(std::clamp(gpu, 0.0, 100.0));
        }
        double vram = 0.0;
        if (vramCounter_ && SumCounter(vramCounter_, vram)) {
            out.vramUsedGb = vram / (1024.0 * 1024.0 * 1024.0);
        }
    }
    out.vramTotalGb = vramTotalGb_;

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.BatteryFlag != 128 && power.BatteryFlag != 255 &&
        power.BatteryLifePercent <= 100) {
        out.batteryPercent = power.BatteryLifePercent;
        out.charging = power.ACLineStatus == 1;
    }
}

}  // namespace lwe
