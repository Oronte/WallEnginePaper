#include "ui/FileDialogs.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

std::optional<std::filesystem::path> RunOpenDialog(HWND owner, bool pickFolder) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (pickFolder) {
        options |= FOS_PICKFOLDERS;
        dialog->SetTitle(L"Ajouter un dossier de fonds d'écran");
    } else {
        options |= FOS_FILEMUSTEXIST;
        dialog->SetTitle(L"Importer un fond d'écran");
        static const COMDLG_FILTERSPEC kFilters[] = {
            {L"Fonds d'écran", L"*.hlsl;*.fx;*.mp4;*.webm;*.mkv;*.mov;*.html;*.htm;*.png;*.jpg;*.jpeg;*.gif;*.webp;project.json"},
            {L"Shaders HLSL", L"*.hlsl;*.fx"},
            {L"Vidéos", L"*.mp4;*.webm;*.mkv;*.mov;*.avi;*.wmv"},
            {L"Pages web", L"*.html;*.htm"},
            {L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"},
            {L"Projet (project.json)", L"project.json"},
        };
        dialog->SetFileTypes(static_cast<UINT>(std::size(kFilters)), kFilters);
    }
    dialog->SetOptions(options);
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;  // annule par l'utilisateur
    }
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

}  // namespace

std::optional<std::filesystem::path> PickWallpaperFile(HWND owner) {
    return RunOpenDialog(owner, false);
}

std::optional<std::filesystem::path> PickFolder(HWND owner) {
    return RunOpenDialog(owner, true);
}

void OpenInShell(const std::filesystem::path& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace lwe
