#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace lwe {

std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

// Minuscules invariantes (insensibles a la culture), adaptees aux chemins et identifiants.
std::wstring ToLower(std::wstring_view text);

std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path Utf8ToPath(std::string_view text);

}  // namespace lwe
