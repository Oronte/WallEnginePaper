#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

// Journal minimal, thread-safe : fichier UTF-8 (%APPDATA%\WallEnginePaper\log.txt) + OutputDebugString.
namespace lwe::log {

enum class Level { Debug, Info, Warn, Error };

void Initialize(const std::filesystem::path& file);
void Shutdown();
void Write(Level level, std::wstring_view message);

template <class... Args>
void Debug(std::wformat_string<Args...> fmt, Args&&... args) {
    Write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Info(std::wformat_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Warn(std::wformat_string<Args...> fmt, Args&&... args) {
    Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Error(std::wformat_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace lwe::log
