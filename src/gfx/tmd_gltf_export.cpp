#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include "tmd_gltf_export.h"
#include "stb_image_write.h"
#include "texture_atlas.h"
#include "tmd_export_material.h"
#include "tmd_space.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace gfx::TmdGltfExport {

using gfx::ExportMaterial;
using gfx::MakeMaterialKey;
using gfx::SameMaterial;

namespace {

// This file's own local Vec3f (not gfx::Vec3), thin wrapper over
// gfx::ToExportSpace (tmd_space.h) - see tmd_obj_export.cpp's identical
// comment on why it's not just gfx::Vec3 directly.
struct Vec3f { float x, y, z; };
Vec3f ToExportSpace(const tmd::TMD_Vertex& v) { gfx::Vec3 p = gfx::ToExportSpace(v); return { p.x, p.y, p.z }; }
Vec3f ToExportSpace(const tmd::TMD_Normal& n) { gfx::Vec3 p = gfx::ToExportSpace(n); return { p.x, p.y, p.z }; }

} // namespace

bool Export(const tmd::TMD_Model& model, const std::vector<int>& object_indices, const VRAMManager& vram_manager,
            const std::string& out_folder, const std::string& base_name, const Options& options) {
    if (object_indices.empty()) return false;

    // --- Pass 1: collect distinct materials, same as the OBJ exporter. ---
    std::vector<ExportMaterial> materials;
    int flat_counter = 0;
    for (int obj_index : object_indices) {
        if (obj_index < 0 || obj_index >= static_cast<int>(model.objects.size())) continue;
        for (const auto& poly : model.objects[obj_index].polygons) {
            ExportMaterial key = MakeMaterialKey(poly);
            bool found = std::any_of(materials.begin(), materials.end(),
                                      [&](const ExportMaterial& m) { return SameMaterial(m, key); });
            if (found) continue;

            char name[64];
            if (key.textured) snprintf(name, sizeof(name), "mat_tsb%04x_cba%04x", key.tsb, key.cba);
            else snprintf(name, sizeof(name), "mat_flat_%d", flat_counter++);
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
            atlas_tiles[key] = gfx::AtlasTile{};
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

    tinygltf::Model gltf;
    gltf.asset.version = "2.0";
    gltf.asset.generator = "TIM Editor";

    // --- One sampler (nearest filtering - matches the PS1's lack of
    // texture filtering) shared by every textured material. ---
    int sampler_index = -1;
    {
        tinygltf::Sampler sampler;
        sampler.magFilter = TINYGLTF_TEXTURE_FILTER_NEAREST;
        sampler.minFilter = TINYGLTF_TEXTURE_FILTER_NEAREST;
        gltf.samplers.push_back(sampler);
        sampler_index = 0;
    }

    // --- One glTF material per ExportMaterial. When atlas-packed, every
    // textured material shares the same one Image/Texture pair instead of
    // each getting its own - created lazily on the first textured material
    // that needs it. ---
    int atlas_texture_index = -1;
    std::vector<int> material_indices(materials.size(), -1);
    for (size_t i = 0; i < materials.size(); i++) {
        const auto& m = materials[i];
        tinygltf::Material gm;
        gm.name = m.name;
        gm.pbrMetallicRoughness.metallicFactor = 0.0;
        gm.pbrMetallicRoughness.roughnessFactor = 1.0;
        if (m.textured) {
            gm.pbrMetallicRoughness.baseColorFactor = { 1.0, 1.0, 1.0, 1.0 };
            if (use_atlas) {
                if (atlas_texture_index < 0 && !atlas_tiles.empty()) {
                    tinygltf::Image image;
                    image.uri = atlas_filename;
                    gltf.images.push_back(image);
                    tinygltf::Texture texture;
                    texture.source = static_cast<int>(gltf.images.size()) - 1;
                    texture.sampler = sampler_index;
                    gltf.textures.push_back(texture);
                    atlas_texture_index = static_cast<int>(gltf.textures.size()) - 1;
                }
                if (atlas_texture_index >= 0) gm.pbrMetallicRoughness.baseColorTexture.index = atlas_texture_index;
            } else if (options.bake_textures) {
                tinygltf::Image image;
                image.uri = m.name + ".png";
                gltf.images.push_back(image);
                tinygltf::Texture texture;
                texture.source = static_cast<int>(gltf.images.size()) - 1;
                texture.sampler = sampler_index;
                gltf.textures.push_back(texture);
                gm.pbrMetallicRoughness.baseColorTexture.index = static_cast<int>(gltf.textures.size()) - 1;
            }
        } else {
            gm.pbrMetallicRoughness.baseColorFactor = { m.color[0][0] / 255.0, m.color[0][1] / 255.0,
                                                         m.color[0][2] / 255.0, 1.0 };
        }
        if (m.semi_transparent) {
            gm.alphaMode = "BLEND";
        }
        gltf.materials.push_back(gm);
        material_indices[i] = static_cast<int>(gltf.materials.size()) - 1;
    }

    // --- One shared binary buffer, filled incrementally per primitive group. ---
    std::vector<unsigned char> buffer_data;

    auto AppendBufferView = [&](const void* data, size_t byte_length, int target) -> int {
        size_t offset = buffer_data.size();
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        buffer_data.insert(buffer_data.end(), bytes, bytes + byte_length);
        tinygltf::BufferView bv;
        bv.buffer = 0;
        bv.byteOffset = offset;
        bv.byteLength = byte_length;
        bv.target = target;
        gltf.bufferViews.push_back(bv);
        return static_cast<int>(gltf.bufferViews.size()) - 1;
    };
    auto AddAccessor = [&](int buffer_view, int component_type, size_t count, int type, std::vector<double> min = {},
                           std::vector<double> max = {}) -> int {
        tinygltf::Accessor acc;
        acc.bufferView = buffer_view;
        acc.componentType = component_type;
        acc.count = count;
        acc.type = type;
        acc.minValues = std::move(min);
        acc.maxValues = std::move(max);
        gltf.accessors.push_back(acc);
        return static_cast<int>(gltf.accessors.size()) - 1;
    };

    for (int obj_index : object_indices) {
        if (obj_index < 0 || obj_index >= static_cast<int>(model.objects.size())) continue;
        const tmd::TMD_Object& tmd_obj = model.objects[obj_index];

        tinygltf::Mesh mesh;

        for (size_t mi = 0; mi < materials.size(); mi++) {
            const ExportMaterial& mat = materials[mi];
            std::vector<const tmd::TMD_Polygon*> group;
            for (const auto& poly : tmd_obj.polygons) {
                if (SameMaterial(MakeMaterialKey(poly), mat)) group.push_back(&poly);
            }
            if (group.empty()) continue;

            std::vector<float> positions, normals, uvs, colors;
            std::vector<uint32_t> indices;
            uint32_t running_vertex = 0;
            float pos_min[3] = { 1e9f, 1e9f, 1e9f }, pos_max[3] = { -1e9f, -1e9f, -1e9f };

            const gfx::AtlasTile* atlas_tile = nullptr;
            if (use_atlas && mat.textured) {
                uint32_t key = (static_cast<uint32_t>(mat.tsb) << 16) | mat.cba;
                auto it = atlas_tiles.find(key);
                if (it != atlas_tiles.end()) atlas_tile = &it->second;
            }

            for (const auto* poly_ptr : group) {
                const auto& poly = *poly_ptr;
                for (int i = 0; i < poly.num_verts; i++) {
                    uint16_t vi = poly.vert_idx[i];
                    Vec3f p = (vi < tmd_obj.vertices.size()) ? ToExportSpace(tmd_obj.vertices[vi]) : Vec3f{ 0, 0, 0 };
                    positions.push_back(p.x);
                    positions.push_back(p.y);
                    positions.push_back(p.z);
                    pos_min[0] = std::min(pos_min[0], p.x); pos_min[1] = std::min(pos_min[1], p.y); pos_min[2] = std::min(pos_min[2], p.z);
                    pos_max[0] = std::max(pos_max[0], p.x); pos_max[1] = std::max(pos_max[1], p.y); pos_max[2] = std::max(pos_max[2], p.z);

                    uint16_t ni = poly.gouraud ? poly.norm_idx[i] : poly.norm_idx[0];
                    Vec3f n = (!poly.no_light && ni != tmd::TMD_Polygon::kNoNormal && ni < tmd_obj.normals.size())
                                  ? ToExportSpace(tmd_obj.normals[ni])
                                  : Vec3f{ 0, 1, 0 };
                    normals.push_back(n.x);
                    normals.push_back(n.y);
                    normals.push_back(n.z);

                    if (mat.textured && atlas_tile) {
                        uvs.push_back((atlas_tile->x + poly.u[i]) / static_cast<float>(atlas_width));
                        uvs.push_back(1.0f - (atlas_tile->y + poly.v[i]) / static_cast<float>(atlas_height));
                    } else if (mat.textured) {
                        uvs.push_back(poly.u[i] / static_cast<float>(mat.tile_width));
                        uvs.push_back(1.0f - poly.v[i] / 256.0f);
                    } else if (options.include_colors) {
                        int c = poly.color_per_vertex ? i : 0;
                        colors.push_back(poly.color[c][0] / 255.0f);
                        colors.push_back(poly.color[c][1] / 255.0f);
                        colors.push_back(poly.color[c][2] / 255.0f);
                    }
                }

                // Same two triangles GL_TRIANGLE_STRIP actually renders for
                // a strip-order quad (see tmd_object_renderer.cpp) - glTF
                // has no quad primitive, so this is a plain triangle split,
                // not the OBJ path's perimeter-order conversion.
                uint32_t b = running_vertex;
                if (poly.num_verts == 4) {
                    indices.insert(indices.end(), { b + 0, b + 1, b + 2, b + 1, b + 3, b + 2 });
                } else {
                    indices.insert(indices.end(), { b + 0, b + 1, b + 2 });
                }
                running_vertex += poly.num_verts;
            }

            tinygltf::Primitive prim;
            prim.mode = TINYGLTF_MODE_TRIANGLES;
            prim.material = material_indices[mi];

            int pos_view = AppendBufferView(positions.data(), positions.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
            prim.attributes["POSITION"] = AddAccessor(pos_view, TINYGLTF_COMPONENT_TYPE_FLOAT, running_vertex,
                                                       TINYGLTF_TYPE_VEC3,
                                                       { pos_min[0], pos_min[1], pos_min[2] },
                                                       { pos_max[0], pos_max[1], pos_max[2] });

            int norm_view = AppendBufferView(normals.data(), normals.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
            prim.attributes["NORMAL"] =
                AddAccessor(norm_view, TINYGLTF_COMPONENT_TYPE_FLOAT, running_vertex, TINYGLTF_TYPE_VEC3);

            if (!uvs.empty()) {
                int uv_view = AppendBufferView(uvs.data(), uvs.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
                prim.attributes["TEXCOORD_0"] =
                    AddAccessor(uv_view, TINYGLTF_COMPONENT_TYPE_FLOAT, running_vertex, TINYGLTF_TYPE_VEC2);
            }
            if (!colors.empty()) {
                int col_view = AppendBufferView(colors.data(), colors.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
                prim.attributes["COLOR_0"] =
                    AddAccessor(col_view, TINYGLTF_COMPONENT_TYPE_FLOAT, running_vertex, TINYGLTF_TYPE_VEC3);
            }

            int idx_view =
                AppendBufferView(indices.data(), indices.size() * sizeof(uint32_t), TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
            prim.indices =
                AddAccessor(idx_view, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, indices.size(), TINYGLTF_TYPE_SCALAR);

            mesh.primitives.push_back(prim);
        }

        if (mesh.primitives.empty()) continue;
        gltf.meshes.push_back(mesh);

        tinygltf::Node node;
        node.name = "Object" + std::to_string(obj_index);
        node.mesh = static_cast<int>(gltf.meshes.size()) - 1;
        gltf.nodes.push_back(node);
    }

    if (gltf.nodes.empty()) return false;

    tinygltf::Buffer buffer;
    buffer.data = std::move(buffer_data);
    buffer.uri = base_name + ".bin";
    gltf.buffers.push_back(buffer);

    tinygltf::Scene scene;
    for (size_t i = 0; i < gltf.nodes.size(); i++) scene.nodes.push_back(static_cast<int>(i));
    gltf.scenes.push_back(scene);
    gltf.defaultScene = 0;

    tinygltf::TinyGLTF writer;
    return writer.WriteGltfSceneToFile(&gltf, out_folder + "/" + base_name + ".gltf",
                                        /*embedImages=*/false, /*embedBuffers=*/false, /*prettyPrint=*/true,
                                        /*writeBinary=*/false);
}

} // namespace gfx::TmdGltfExport
