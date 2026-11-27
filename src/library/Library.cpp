#include "library/Library.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <optional>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "wallpaper/WallpaperFactory.h"

namespace lwe {
namespace {

std::wstring PrettifyName(const std::filesystem::path& file) {
    std::wstring name = file.stem().wstring();
    bool startOfWord = true;
    for (auto& c : name) {
        if (c == L'_' || c == L'-' || c == L'.') {
            c = L' ';
            startOfWord = true;
        } else if (startOfWord) {
            c = static_cast<wchar_t>(std::towupper(c));
            startOfWord = false;
        }
    }
    return name;
}

// Metadonnees d'un shader : lignes "// @title ...", "// @description ...", "// @author ..." en tete de fichier.
void ReadShaderMetadata(const std::filesystem::path& file, LibraryItem& item) {
    std::ifstream stream(file, std::ios::binary);
    std::string line;
    for (int i = 0; i < 64 && std::getline(stream, line); ++i) {
        const auto readTag = [&](std::string_view tag, std::wstring& out) {
            const size_t pos = line.find(tag);
            if (pos == std::string::npos) {
                return;
            }
            std::string value = line.substr(pos + tag.size());
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.erase(value.begin());
            }
            while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) {
                value.pop_back();
            }
            if (!value.empty()) {
                out = Utf8ToWide(value);
            }
        };
        readTag("@title", item.title);
        readTag("@description", item.description);
        readTag("@author", item.author);
    }
}

std::optional<WallpaperType> ParseProjectType(std::string type) {
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (type == "shader" || type == "hlsl") {
        return WallpaperType::Shader;
    }
    if (type == "video") {
        return WallpaperType::Video;
    }
    if (type == "web" || type == "html") {
        return WallpaperType::Web;
    }
    if (type == "scene" || type == "image") {
        return WallpaperType::Scene;
    }
    return std::nullopt;
}

bool ReadProject(const std::filesystem::path& folder, LibraryItem& item) {
    std::ifstream stream(folder / L"project.json", std::ios::binary);
    if (!stream) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto json = nlohmann::json::parse(text, nullptr, false, true);
    if (json.is_discarded() || !json.is_object()) {
        log::Warn(L"project.json invalide : {}", folder.wstring());
        return false;
    }
    const auto str = [&](const char* key) -> std::string {
        const auto it = json.find(key);
        return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const std::string file = str("file");
    if (file.empty()) {
        return false;
    }
    item.source = (folder / Utf8ToPath(file)).lexically_normal();
    const auto type = ParseProjectType(str("type"));
    const auto guessed = GuessWallpaperType(item.source);
    if (type) {
        item.type = *type;
    } else if (guessed) {
        item.type = *guessed;
    } else {
        return false;
    }
    item.title = Utf8ToWide(str("title"));
    item.description = Utf8ToWide(str("description"));
    item.author = Utf8ToWide(str("author"));
    if (const std::string preview = str("preview"); !preview.empty()) {
        item.previewImage = (folder / Utf8ToPath(preview)).lexically_normal();
    }
    if (item.title.empty()) {
        item.title = PrettifyName(folder);
    }
    std::error_code ec;
    return std::filesystem::exists(item.source, ec);
}

std::filesystem::path UniqueDestination(const std::filesystem::path& folder, const std::filesystem::path& name) {
    std::error_code ec;
    std::filesystem::path candidate = folder / name;
    for (int i = 2; std::filesystem::exists(candidate, ec); ++i) {
        candidate = folder / (name.stem().wstring() + L" (" + std::to_wstring(i) + L")" + name.extension().wstring());
    }
    return candidate;
}

}  // namespace

WallpaperRef LibraryItem::Ref() const {
    return WallpaperRef{type, MakePortablePath(source)};
}

void Library::Scan(const std::vector<std::filesystem::path>& extraFolders) {
    items_.clear();
    // Fonds livres avec l'application : un sous-dossier par famille (shaders, web, videos, images...).
    std::error_code ec;
    for (std::filesystem::directory_iterator it(ResolveAppPath(L"assets"), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            ScanFolder(it->path(), false);
        }
    }
    ScanFolder(UserLibraryDir(), true);
    for (const auto& folder : extraFolders) {
        ScanFolder(ResolveAppPath(folder), false);
    }
    std::sort(items_.begin(), items_.end(), [](const LibraryItem& a, const LibraryItem& b) {
        return ToLower(a.title) < ToLower(b.title);
    });
    log::Info(L"Bibliotheque : {} fond(s)", items_.size());
}

void Library::ScanFolder(const std::filesystem::path& folder, bool userImported) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::filesystem::path path = it->path();
        LibraryItem item;
        item.userImported = userImported;
        if (it->is_directory(ec)) {
            std::error_code existsError;
            if (std::filesystem::exists(path / L"project.json", existsError)) {
                if (!ReadProject(path, item)) {
                    continue;
                }
            } else if (std::filesystem::exists(path / L"index.html", existsError)) {
                item.type = WallpaperType::Web;
                item.source = path / L"index.html";
                item.title = PrettifyName(path);
            } else {
                continue;
            }
        } else if (it->is_regular_file(ec)) {
            const auto type = GuessWallpaperType(path);
            if (!type) {
                continue;
            }
            item.type = *type;
            item.source = path;
            if (item.type == WallpaperType::Shader) {
                ReadShaderMetadata(path, item);
            }
            if (item.title.empty()) {
                item.title = PrettifyName(path);
            }
        } else {
            continue;
        }
        AddItem(std::move(item));
    }
}

void Library::AddItem(LibraryItem item) {
    item.source = item.source.lexically_normal();
    item.id = WallpaperSettingsKey(item.Ref().source);
    item.supported = IsBackendAvailable(item.type);
    if (FindById(item.id)) {
        return;
    }
    items_.push_back(std::move(item));
}

const LibraryItem* Library::FindBySource(const std::filesystem::path& source) const {
    if (source.empty()) {
        return nullptr;
    }
    return FindById(WallpaperSettingsKey(MakePortablePath(source.lexically_normal())));
}

const LibraryItem* Library::FindById(std::string_view id) const {
    const auto it = std::find_if(items_.begin(), items_.end(), [&](const LibraryItem& item) { return item.id == id; });
    return it == items_.end() ? nullptr : &*it;
}

bool Library::Import(const std::filesystem::path& file, std::filesystem::path& importedSource, std::wstring& error) {
    std::error_code ec;
    const std::filesystem::path destinationRoot = UserLibraryDir();
    if (ToLower(file.filename().wstring()) == L"project.json") {
        const std::filesystem::path folder = file.parent_path();
        const std::filesystem::path destination = UniqueDestination(destinationRoot, folder.filename());
        std::filesystem::copy(folder, destination, std::filesystem::copy_options::recursive, ec);
        if (ec) {
            error = Utf8ToWide(ec.message());
            return false;
        }
        LibraryItem item;
        if (!ReadProject(destination, item)) {
            error = L"project.json invalide ou fichier principal introuvable";
            return false;
        }
        importedSource = item.source;
        return true;
    }
    if (!GuessWallpaperType(file)) {
        error = L"type de fichier non reconnu";
        return false;
    }
    const std::filesystem::path destination = UniqueDestination(destinationRoot, file.filename());
    std::filesystem::copy_file(file, destination, ec);
    if (ec) {
        error = Utf8ToWide(ec.message());
        return false;
    }
    importedSource = destination;
    log::Info(L"Importe : {}", destination.wstring());
    return true;
}

}  // namespace lwe
