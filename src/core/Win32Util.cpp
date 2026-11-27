#include "core/Win32Util.h"

#include <format>

namespace lwe {

HrError::HrError(HRESULT hr, const char* what)
    : std::runtime_error(std::format("{} (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr))), hr_(hr) {}

std::wstring HResultToString(HRESULT hr) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text;
    if (length > 0 && buffer) {
        text.assign(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' ||
                                 text.back() == L'.')) {
            text.pop_back();
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    if (text.empty()) {
        return std::format(L"0x{:08X}", static_cast<unsigned>(hr));
    }
    return std::format(L"0x{:08X} : {}", static_cast<unsigned>(hr), text);
}

}  // namespace lwe
