#include "core/StringUtil.h"

#include <windows.h>

namespace lwe {

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr,
                        nullptr);
    return result;
}

std::wstring ToLower(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr, 0);
    if (length <= 0) {
        return std::wstring(text);
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()), result.data(),
                  length, nullptr, nullptr, 0);
    return result;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return WideToUtf8(path.generic_wstring());
}

std::filesystem::path Utf8ToPath(std::string_view text) {
    return std::filesystem::path(Utf8ToWide(text));
}

}  // namespace lwe
