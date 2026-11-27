#include "core/Log.h"

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <share.h>

#include "core/StringUtil.h"

namespace lwe::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

const wchar_t* LevelTag(Level level) noexcept {
    switch (level) {
    case Level::Debug: return L"DBG";
    case Level::Info: return L"INF";
    case Level::Warn: return L"WRN";
    case Level::Error: return L"ERR";
    }
    return L"???";
}

}  // namespace

void Initialize(const std::filesystem::path& file) {
    std::scoped_lock lock(g_mutex);
    if (g_file) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    // On garde le journal de la session precedente (utile apres un plantage).
    std::filesystem::path previous = file;
    previous += L".old";
    std::filesystem::rename(file, previous, ec);

    g_file = _wfsopen(file.c_str(), L"wb", _SH_DENYWR);
}

void Shutdown() {
    std::scoped_lock lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void Write(Level level, std::wstring_view message) {
#ifdef NDEBUG
    if (level == Level::Debug) {
        return;
    }
#endif
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const std::wstring line = std::format(L"{:02}:{:02}:{:02}.{:03} [{}] {}\n", st.wHour, st.wMinute,
                                          st.wSecond, st.wMilliseconds, LevelTag(level), message);
    OutputDebugStringW(line.c_str());

    std::scoped_lock lock(g_mutex);
    if (g_file) {
        const std::string utf8 = WideToUtf8(line);
        std::fwrite(utf8.data(), 1, utf8.size(), g_file);
        std::fflush(g_file);
    }
}

}  // namespace lwe::log
