#include "tmd_texture_cache.h"
#include "../core/gl_compat.h"

namespace gfx {

TmdTextureCache::~TmdTextureCache() { InvalidateAll(); }

void TmdTextureCache::InvalidateAll() {
    for (auto& e : entries) {
        if (e.tile.gl_tex) glDeleteTextures(1, &e.tile.gl_tex);
    }
    entries.clear();
}

TmdTextureCache::Tile TmdTextureCache::GetTile(const VRAMManager& vram, uint16_t tsb, uint16_t cba) {
    for (const auto& e : entries) {
        if (e.tsb == tsb && e.cba == cba) return e.tile;
    }

    std::vector<uint8_t> rgba;
    int width = 0;
    vram.DecodeTexPage(tsb, cba, rgba, width);

    Tile tile;
    tile.width = width;
    glGenTextures(1, &tile.gl_tex);
    glBindTexture(GL_TEXTURE_2D, tile.gl_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, VRAMManager::kTPageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    entries.push_back({ tsb, cba, tile });
    return tile;
}

} // namespace gfx
