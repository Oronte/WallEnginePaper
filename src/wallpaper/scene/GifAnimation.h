#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lwe {

// GIF anime decode via WIC puis recompose sur le CPU, image par image (decalages, delais, methodes de
// "disposal"). Seules les sous-images sont gardees en memoire ; la toile complete est reconstruite a
// chaque changement d'image, puis envoyee au GPU par le backend.
class GifAnimation {
public:
    // false si le fichier n'est pas un GIF anime (une seule image) ou est illisible.
    bool Load(const std::filesystem::path& file, std::wstring& error);

    uint32_t Width() const noexcept { return width_; }
    uint32_t Height() const noexcept { return height_; }
    size_t FrameCount() const noexcept { return frames_.size(); }

    // Avance de `seconds` ; retourne true si la toile a change.
    bool Advance(double seconds);
    const std::vector<uint8_t>& Canvas() const noexcept { return canvas_; }  // RGBA 8 bits

private:
    struct Frame {
        std::vector<uint8_t> rgba;
        uint32_t left = 0;
        uint32_t top = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        double delay = 0.1;
        uint8_t disposal = 0;  // 0/1 : conserver, 2 : effacer la zone, 3 : restaurer l'etat precedent
    };

    void DrawFrame(size_t index);
    void DisposeFrame(size_t index);

    std::vector<Frame> frames_;
    std::vector<uint8_t> canvas_;
    std::vector<uint8_t> previous_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    size_t current_ = 0;
    double timeInFrame_ = 0.0;
};

}  // namespace lwe
