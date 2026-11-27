#include "system/GpuUsage.h"

#include <pdhmsg.h>

#include <format>
#include <string>
#include <vector>

namespace lwe {
namespace {

double SumInstances(PDH_HCOUNTER counter) {
    DWORD size = 0;
    DWORD count = 0;
    if (!counter || PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) != PDH_MORE_DATA) {
        return -1.0;
    }
    std::vector<uint8_t> buffer(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) {
        return -1.0;
    }
    double sum = 0.0;
    for (DWORD i = 0; i < count; ++i) {
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
            sum += items[i].FmtValue.doubleValue;
        }
    }
    return sum;
}

}  // namespace

bool GpuUsage::Start() {
    if (query_) {
        return true;
    }
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
        query_ = nullptr;
        return false;
    }
    // Instances "pid_<pid>_luid_..._engtype_3D" : uniquement les moteurs utilises par ce processus.
    const DWORD pid = GetCurrentProcessId();
    const std::wstring render = std::format(L"\\GPU Engine(pid_{}_*engtype_3D)\\Utilization Percentage", pid);
    const std::wstring decode = std::format(L"\\GPU Engine(pid_{}_*engtype_VideoDecode)\\Utilization Percentage", pid);
    if (PdhAddEnglishCounterW(query_, render.c_str(), 0, &render_) != ERROR_SUCCESS) {
        render_ = nullptr;
    }
    if (PdhAddEnglishCounterW(query_, decode.c_str(), 0, &decode_) != ERROR_SUCCESS) {
        decode_ = nullptr;
    }
    PdhCollectQueryData(query_);
    return render_ != nullptr;
}

void GpuUsage::Stop() {
    if (query_) {
        PdhCloseQuery(query_);
    }
    query_ = nullptr;
    render_ = decode_ = nullptr;
}

void GpuUsage::Sample(double& render3d, double& videoDecode) {
    render3d = videoDecode = -1.0;
    if (!query_ || PdhCollectQueryData(query_) != ERROR_SUCCESS) {
        return;
    }
    render3d = SumInstances(render_);
    videoDecode = SumInstances(decode_);
}

}  // namespace lwe
