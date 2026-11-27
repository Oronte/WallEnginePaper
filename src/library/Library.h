#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "config/AppConfig.h"

namespace lwe {

struct LibraryItem {
    std::string id;  // == WallpaperSettingsKey(Ref().source) : cle des reglages dans config.json
    std::wstring title;
    std::wstring description;
    std::wstring author;
    WallpaperType type = WallpaperType::Shader;
    std::filesystem::path source;        // chemin absolu du fichier a afficher
    std::filesystem::path previewImage;  // image d'apercu optionnelle (project.json "preview")
    bool supported = false;              // backend disponible dans cette build
    bool userImported = false;           // situe dans %APPDATA%\WallEnginePaper\library

    WallpaperRef Ref() const;
};

// Catalogue local des fonds d'ecran. Sources analysees :
//   - <exe>\assets\<famille>          (fonds livres avec l'application : shaders, web...)
//   - %APPDATA%\WallEnginePaper\library (fonds importes)
//   - les dossiers ajoutes par l'utilisateur (config.libraryFolders)
// Dans chaque dossier : les fichiers reconnus (.hlsl, .mp4/.webm/.mkv..., .html, images) et les
// sous-dossiers contenant un project.json { "title", "type", "file", "preview", "description", "author" }
// ou un index.html.
class Library {
public:
    void Scan(const std::vector<std::filesystem::path>& extraFolders);

    const std::vector<LibraryItem>& Items() const noexcept { return items_; }
    const LibraryItem* FindBySource(const std::filesystem::path& source) const;
    const LibraryItem* FindById(std::string_view id) const;

    // Copie un fichier (ou le dossier d'un project.json) dans la bibliotheque utilisateur.
    static bool Import(const std::filesystem::path& file, std::filesystem::path& importedSource, std::wstring& error);

private:
    void ScanFolder(const std::filesystem::path& folder, bool userImported);
    void AddItem(LibraryItem item);

    std::vector<LibraryItem> items_;
};

}  // namespace lwe
