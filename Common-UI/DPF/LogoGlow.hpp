#pragma once

#include "../LogoGlow.hpp"
#include "../SkinFileIO.hpp"
#include "Theme.hpp"

#include <memory>

// Reuse the decoder already linked by the pinned DPF NanoVG implementation.
// Its symbols are prefixed to avoid colliding with a host's own stb_image.
extern "C" {
unsigned char* dpf_stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
int dpf_stbi_info_from_memory(const unsigned char*, int, int*, int*, int*);
void dpf_stbi_image_free(void*);
}

namespace sms::ui::dpf {

inline bool logoGlowEnabled(const Theme& colors) noexcept
{
    return colors.logoGlowAlpha > 0.0f && colors.logoGlowRadius > 0.0f && colors.logoGlow.alpha > 0.0f;
}

// Only call while loading the editor's images, with a current graphics context.
inline std::vector<std::uint8_t> loadLogoGlow(std::span<const std::uint8_t> image, float radius)
{
    int width = 0, height = 0, channels = 0;
    if (image.empty() || image.size() > skinFileIO::maxImageBytes) return {};
    const bool valid = dpf_stbi_info_from_memory(image.data(), static_cast<int>(image.size()),
                                                &width, &height, &channels) != 0;
    if (!valid || width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return {};
    std::unique_ptr<unsigned char, decltype(&dpf_stbi_image_free)> pixels(
        dpf_stbi_load_from_memory(image.data(), static_cast<int>(image.size()),
                                  &width, &height, &channels, 4),
        &dpf_stbi_image_free);
    if (!pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return {};
    return makeLogoGlow({pixels.get(), static_cast<std::size_t>(width) * height * 4},
                        width, height, radius);
}

// An empty path means the embedded original; use the path only if its image loaded.
inline std::vector<std::uint8_t> loadLogoGlow(const char* path,
                                            std::span<const std::uint8_t> original,
                                            float radius)
{
    if (path == nullptr || *path == '\0') return loadLogoGlow(original, radius);
    const auto bytes = skinFileIO::readImage(skinFileIO::pathFromUtf8(path));
    return loadLogoGlow(bytes, radius);
}

} // namespace sms::ui::dpf
