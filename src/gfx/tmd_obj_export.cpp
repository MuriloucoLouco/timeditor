#include "tmd_obj_export.h"
#include "stb_image_write.h"
#include "texture_atlas.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

namespace gfx::TmdObjExport {

namespace {

// One distinct combination of PS1-specific polygon attributes an exported
// material stands in for - purely to group polygons for the .mtl/baked-
// texture output. Export-only: nothing reads this back on import (see
// tmd_mesh_import.h - reimporting always rebuilds fresh instead of trying
// to recover the original attributes).
struct ExportMaterial {
    std::string name;
    uint16_t tsb = 0;
    uint16_t cba = 0;
    int tile_width = 256;
    bool textured = false;
    bool semi_transparent = false;
    bool color_per_vertex = false;
    uint8_t color[4][3] = { { 128, 128, 128 }, { 128, 128, 128 }, { 128, 128, 128 }, { 128, 128, 128 } };
};

int TileWidthForTsb(uint16_t tsb) {
    int color_mode = (tsb >> 7) & 0x3;
    return color_mode == 0 ? 256 : color_mode == 1 ? 128 : 64;
}

ExportMaterial MakeMaterialKey(const tmd::TMD_Polygon& p) {
    ExportMaterial m;
    m.tsb = p.tsb;
    m.cba = p.cba;
    m.tile_width = p.textured ? TileWidthForTsb(p.tsb) : 256;
    m.textured = p.textured;
    m.semi_transparent = p.semi_transparent;
    m.color_per_vertex = p.color_per_vertex;
    for (int i = 0; i < 4; i++) {
        m.color[i][0] = p.color[i][0];
        m.color[i][1] = p.color[i][1];
        m.color[i][2] = p.color[i][2];
    }
    return m;
}

bool SameMaterial(const ExportMaterial& a, const ExportMaterial& b) {
    if (a.tsb != b.tsb || a.cba != b.cba || a.textured != b.textured || a.semi_transparent != b.semi_transparent ||
        a.color_per_vertex != b.color_per_vertex) {
        return false;
    }
    int n = a.color_per_vertex ? 4 : 1;
    for (int i = 0; i < n; i++) {
        if (a.color[i][0] != b.color[i][0] || a.color[i][1] != b.color[i][1] || a.color[i][2] != b.color[i][2]) {
            return false;
        }
    }
    return true;
}

// Cross product of the face's first two edges, normalized - a reasonable
// per-face reference normal for a no_light polygon (which has no GTE
// normal of its own) to show in the external DCC tool. Never read back on
// reimport, so it doesn't need to relate to the TMD's own normal table.
struct Vec3f { float x, y, z; };
Vec3f Sub(Vec3f a, Vec3f b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3f Cross(Vec3f a, Vec3f b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vec3f Normalize(Vec3f v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) return { 0, 1, 0 };
    return { v.x / len, v.y / len, v.z / len };
}

// PS-X object space is Y-down; negate Y so the exported mesh looks upright
// in a conventional Y-up DCC tool - inverted symmetrically on reimport.
Vec3f ToExportSpace(const tmd::TMD_Vertex& v) {
    return { static_cast<float>(v.x), -static_cast<float>(v.y), static_cast<float>(v.z) };
}
Vec3f ToExportSpace(const tmd::TMD_Normal& n) {
    return { static_cast<float>(n.x), -static_cast<float>(n.y), static_cast<float>(n.z) };
}

} // namespace

bool ExportForEditing(const tmd::TMD_Model& model, const std::vector<int>& object_indices,
                      const VRAMManager& vram_manager, const std::string& out_folder, const std::string& base_name,
                      const Options& options) {
    if (object_indices.empty()) return false;

    // --- Pass 1: collect distinct materials across every exported polygon. ---
    std::vector<ExportMaterial> materials;
    int flat_counter = 0;
    for (int obj_index : object_indices) {
        if (obj_index < 0 || obj_index >= static_cast<int>(model.objects.size())) continue;
        for (const auto& poly : model.objects[obj_index].polygons) {
            ExportMaterial key = MakeMaterialKey(poly);
            bool found = false;
            for (const auto& m : materials) {
                if (SameMaterial(m, key)) { found = true; break; }
            }
            if (!found) {
                char name[64];
                if (key.textured) snprintf(name, sizeof(name), "mat_tsb%04x_cba%04x", key.tsb, key.cba);
                else snprintf(name, sizeof(name), "mat_flat_%d", flat_counter++);

                // Two distinct materials (differing in color, say) can
                // share the same tsb/cba and so collide on this name -
                // must stay unique so usemtl lines unambiguously pick one.
                std::string unique_name = name;
                int suffix = 1;
                while (std::any_of(materials.begin(), materials.end(),
                                    [&](const ExportMaterial& m) { return m.name == unique_name; })) {
                    unique_name = std::string(name) + "_" + std::to_string(++suffix);
                }

                key.name = unique_name;
                materials.push_back(key);
            }
        }
    }

    // --- Bake reference texture(s): either one atlas PNG shared by every
    // textured material, or one PNG per material (see Options::pack_atlas). ---
    std::string atlas_filename = base_name + "_atlas.png";
    std::map<uint32_t, gfx::AtlasTile> atlas_tiles; // key: tsb<<16 | cba
    int atlas_width = 0, atlas_height = 0;
    bool use_atlas = options.bake_textures && options.pack_atlas;
    if (use_atlas) {
        std::vector<std::pair<uint16_t, uint16_t>> keys;
        for (const auto& m : materials) {
            if (!m.textured) continue;
            uint32_t key = (static_cast<uint32_t>(m.tsb) << 16) | m.cba;
            if (atlas_tiles.count(key)) continue;
            atlas_tiles[key] = gfx::AtlasTile{}; // placeholder, filled in below
            keys.push_back({ m.tsb, m.cba });
        }
        if (!keys.empty()) {
            gfx::AtlasResult atlas = gfx::PackTextureAtlas(vram_manager, keys);
            atlas_width = atlas.width;
            atlas_height = atlas.height;
            for (const auto& tile : atlas.tiles) {
                uint32_t key = (static_cast<uint32_t>(tile.tsb) << 16) | tile.cba;
                atlas_tiles[key] = tile;
            }
            std::string png_path = out_folder + "/" + atlas_filename;
            stbi_write_png(png_path.c_str(), atlas.width, atlas.height, 4, atlas.rgba.data(), atlas.width * 4);
        }
    } else if (options.bake_textures) {
        for (const auto& m : materials) {
            if (!m.textured) continue;
            std::vector<uint8_t> rgba;
            int width = 0;
            vram_manager.DecodeTexPage(m.tsb, m.cba, rgba, width);
            std::string png_path = out_folder + "/" + m.name + ".png";
            stbi_write_png(png_path.c_str(), width, VRAMManager::kTPageHeight, 4, rgba.data(), width * 4);
        }
    }

    // --- Write the .mtl. ---
    {
        std::ofstream mtl(out_folder + "/" + base_name + ".mtl");
        if (!mtl) return false;
        for (const auto& m : materials) {
            mtl << "newmtl " << m.name << "\n";
            if (m.textured) {
                mtl << "Kd 1 1 1\n";
                if (options.bake_textures) mtl << "map_Kd " << (use_atlas ? atlas_filename : m.name + ".png") << "\n";
            } else {
                mtl << "Kd " << (m.color[0][0] / 255.0f) << " " << (m.color[0][1] / 255.0f) << " "
                    << (m.color[0][2] / 255.0f) << "\n";
            }
            if (m.semi_transparent) mtl << "d 0.75\n";
            mtl << "\n";
        }
    }

    // --- Write the .obj. ---
    std::ofstream obj(out_folder + "/" + base_name + ".obj");
    if (!obj) return false;
    obj << "# Exported by TIM Editor for external UV/mesh editing.\n";
    obj << "mtllib " << base_name << ".mtl\n";

    int vertex_base = 0;
    int normal_base = 0;
    int vt_counter = 0;
    std::string current_material;

    for (int obj_index : object_indices) {
        if (obj_index < 0 || obj_index >= static_cast<int>(model.objects.size())) continue;
        const tmd::TMD_Object& tmd_obj = model.objects[obj_index];

        // For "Include flat colors": OBJ's extended v-line color is per-
        // vertex, but TMD color is per-polygon-corner, so a shared vertex
        // referenced by several untextured polygons can only carry one
        // color out - the first untextured polygon to touch it wins. Good
        // enough for a re-import fallback; not meant to be exact.
        std::vector<bool> vertex_has_color(tmd_obj.vertices.size(), false);
        std::vector<Vec3f> vertex_color(tmd_obj.vertices.size(), Vec3f{ 0, 0, 0 });
        if (options.include_colors) {
            for (const auto& poly : tmd_obj.polygons) {
                if (poly.textured) continue;
                for (int i = 0; i < poly.num_verts; i++) {
                    uint16_t vi = poly.vert_idx[i];
                    if (vi >= tmd_obj.vertices.size() || vertex_has_color[vi]) continue;
                    int c = poly.color_per_vertex ? i : 0;
                    vertex_has_color[vi] = true;
                    vertex_color[vi] = { poly.color[c][0] / 255.0f, poly.color[c][1] / 255.0f, poly.color[c][2] / 255.0f };
                }
            }
        }

        obj << "o Object" << obj_index << "\n";
        for (size_t i = 0; i < tmd_obj.vertices.size(); i++) {
            Vec3f p = ToExportSpace(tmd_obj.vertices[i]);
            obj << "v " << p.x << " " << p.y << " " << p.z;
            if (vertex_has_color[i]) obj << " " << vertex_color[i].x << " " << vertex_color[i].y << " " << vertex_color[i].z;
            obj << "\n";
        }
        for (const auto& n : tmd_obj.normals) {
            Vec3f p = ToExportSpace(n);
            obj << "vn " << p.x << " " << p.y << " " << p.z << "\n";
        }

        // One extra computed normal per no_light face (see MakeMaterialKey/
        // ToExportSpace comments) - collected up front so their vn lines
        // can be written together right after the object's real ones.
        std::vector<Vec3f> extra_normals;
        std::vector<int> extra_normal_for_poly(tmd_obj.polygons.size(), -1);
        for (size_t p = 0; p < tmd_obj.polygons.size(); p++) {
            const auto& poly = tmd_obj.polygons[p];
            if (!poly.no_light) continue;
            if (poly.vert_idx[0] >= tmd_obj.vertices.size() || poly.vert_idx[1] >= tmd_obj.vertices.size() ||
                poly.vert_idx[2] >= tmd_obj.vertices.size()) {
                continue;
            }
            Vec3f v0 = ToExportSpace(tmd_obj.vertices[poly.vert_idx[0]]);
            Vec3f v1 = ToExportSpace(tmd_obj.vertices[poly.vert_idx[1]]);
            Vec3f v2 = ToExportSpace(tmd_obj.vertices[poly.vert_idx[2]]);
            Vec3f n = Normalize(Cross(Sub(v1, v0), Sub(v2, v0)));
            extra_normal_for_poly[p] = static_cast<int>(extra_normals.size());
            extra_normals.push_back(n);
        }
        for (const auto& n : extra_normals) {
            obj << "vn " << n.x << " " << n.y << " " << n.z << "\n";
        }
        int extra_normal_base = normal_base + static_cast<int>(tmd_obj.normals.size());

        for (size_t p = 0; p < tmd_obj.polygons.size(); p++) {
            const auto& poly = tmd_obj.polygons[p];
            ExportMaterial key = MakeMaterialKey(poly);
            const ExportMaterial* mat = nullptr;
            for (const auto& m : materials) {
                if (SameMaterial(m, key)) { mat = &m; break; }
            }
            std::string mat_name = mat ? mat->name : "mat_flat_0";
            if (mat_name != current_material) {
                obj << "usemtl " << mat_name << "\n";
                current_material = mat_name;
            }

            // OBJ's `f` line needs perimeter order, not TMD's native
            // triangle-strip storage order - see tmd::kQuadPerimeterOrder.
            int tile_width = mat ? mat->tile_width : 256;
            const gfx::AtlasTile* atlas_tile = nullptr;
            if (use_atlas && mat && mat->textured) {
                uint32_t key = (static_cast<uint32_t>(mat->tsb) << 16) | mat->cba;
                auto it = atlas_tiles.find(key);
                if (it != atlas_tiles.end()) atlas_tile = &it->second;
            }
            int vt_indices[4];
            for (int k = 0; k < poly.num_verts; k++) {
                int c = (poly.num_verts == 4) ? tmd::kQuadPerimeterOrder[k] : k;
                float u, v;
                if (atlas_tile) {
                    u = (atlas_tile->x + poly.u[c]) / static_cast<float>(atlas_width);
                    v = 1.0f - (atlas_tile->y + poly.v[c]) / static_cast<float>(atlas_height);
                } else {
                    u = poly.u[c] / static_cast<float>(tile_width);
                    v = 1.0f - poly.v[c] / 256.0f;
                }
                obj << "vt " << u << " " << v << "\n";
                vt_indices[k] = ++vt_counter;
            }

            obj << "f";
            for (int k = 0; k < poly.num_verts; k++) {
                int c = (poly.num_verts == 4) ? tmd::kQuadPerimeterOrder[k] : k;
                int vi = vertex_base + poly.vert_idx[c] + 1;
                int ni;
                if (poly.no_light) ni = extra_normal_base + extra_normal_for_poly[p] + 1;
                else if (poly.gouraud) ni = normal_base + poly.norm_idx[c] + 1;
                else ni = normal_base + poly.norm_idx[0] + 1;
                obj << " " << vi << "/" << vt_indices[k] << "/" << ni;
            }
            obj << "\n";
        }

        vertex_base += static_cast<int>(tmd_obj.vertices.size());
        normal_base = extra_normal_base + static_cast<int>(extra_normals.size());
    }

    return true;
}

} // namespace gfx::TmdObjExport
