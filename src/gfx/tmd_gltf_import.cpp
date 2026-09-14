#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include "tmd_gltf_import.h"
#include <algorithm>
#include <cstring>

namespace gfx::TmdGltfImport {

namespace {

bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool LoadGltf(const std::string& path, tinygltf::Model& model, std::string& error) {
    tinygltf::TinyGLTF loader;
    std::string warn;
    bool ok;
    if (EndsWith(path, ".glb") || EndsWith(path, ".GLB")) {
        ok = loader.LoadBinaryFromFile(&model, &error, &warn, path);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &error, &warn, path);
    }
    return ok;
}

// Reads a VEC2/VEC3/VEC4 float accessor into a flat array, honoring a
// (possibly interleaved) bufferView's byte stride.
std::vector<float> ReadFloatAttribute(const tinygltf::Model& gltf, int accessor_index, int num_components) {
    std::vector<float> out;
    if (accessor_index < 0 || accessor_index >= static_cast<int>(gltf.accessors.size())) return out;
    const auto& acc = gltf.accessors[accessor_index];
    if (acc.bufferView < 0 || acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return out;
    const auto& view = gltf.bufferViews[acc.bufferView];
    const auto& buf = gltf.buffers[view.buffer];

    int stride = acc.ByteStride(view);
    if (stride <= 0) stride = num_components * static_cast<int>(sizeof(float));
    const unsigned char* base = buf.data.data() + view.byteOffset + acc.byteOffset;

    out.resize(acc.count * num_components);
    for (size_t i = 0; i < acc.count; i++) {
        const float* src = reinterpret_cast<const float*>(base + i * static_cast<size_t>(stride));
        for (int c = 0; c < num_components; c++) out[i * num_components + c] = src[c];
    }
    return out;
}

std::vector<uint32_t> ReadIndices(const tinygltf::Model& gltf, int accessor_index) {
    std::vector<uint32_t> out;
    if (accessor_index < 0 || accessor_index >= static_cast<int>(gltf.accessors.size())) return out;
    const auto& acc = gltf.accessors[accessor_index];
    if (acc.bufferView < 0) return out;
    const auto& view = gltf.bufferViews[acc.bufferView];
    const auto& buf = gltf.buffers[view.buffer];
    const unsigned char* base = buf.data.data() + view.byteOffset + acc.byteOffset;

    out.resize(acc.count);
    int stride = acc.ByteStride(view);
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        if (stride <= 0) stride = 1;
        for (size_t i = 0; i < acc.count; i++) out[i] = base[i * stride];
    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        if (stride <= 0) stride = 2;
        for (size_t i = 0; i < acc.count; i++) out[i] = *reinterpret_cast<const uint16_t*>(base + i * stride);
    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        if (stride <= 0) stride = 4;
        for (size_t i = 0; i < acc.count; i++) out[i] = *reinterpret_cast<const uint32_t*>(base + i * stride);
    }
    return out;
}

// Builds one GenericMeshObject per named "ObjectN" node with a mesh,
// combining all of that mesh's TRIANGLES-mode primitives - glTF has no
// quad primitive, so every face comes in (and stays) a triangle; unlike
// OBJ import, there's no n-gon splitting to do here.
std::vector<GenericMeshObject> BuildGenericMeshObjects(const tinygltf::Model& gltf) {
    std::vector<GenericMeshObject> result;

    for (const auto& node : gltf.nodes) {
        if (node.mesh < 0 || node.mesh >= static_cast<int>(gltf.meshes.size())) continue;
        // A name that doesn't parse (renamed, or never following the
        // "ObjectN" convention to begin with) still gets a mesh built for
        // it - Analyze/Apply's ResolveUnmatchedObjects then tries to match
        // it to a TMD object by shape rather than dropping it outright.
        int object_index = ParseTmdObjectIndex(node.name);

        GenericMeshObject mesh_obj;
        mesh_obj.object_index = object_index;

        for (const auto& prim : gltf.meshes[node.mesh].primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) continue;
            auto pos_it = prim.attributes.find("POSITION");
            if (pos_it == prim.attributes.end()) continue;

            std::vector<float> positions = ReadFloatAttribute(gltf, pos_it->second, 3);
            size_t vertex_count = positions.size() / 3;

            std::vector<float> normals;
            auto norm_it = prim.attributes.find("NORMAL");
            if (norm_it != prim.attributes.end()) normals = ReadFloatAttribute(gltf, norm_it->second, 3);

            std::vector<float> uvs;
            auto uv_it = prim.attributes.find("TEXCOORD_0");
            if (uv_it != prim.attributes.end()) uvs = ReadFloatAttribute(gltf, uv_it->second, 2);

            std::vector<float> colors;
            auto col_it = prim.attributes.find("COLOR_0");
            if (col_it != prim.attributes.end()) {
                // COLOR_0 may be VEC3 or VEC4 - try VEC3 first, fall back to VEC4.
                int comp = (col_it->second >= 0 && col_it->second < static_cast<int>(gltf.accessors.size()) &&
                            gltf.accessors[col_it->second].type == TINYGLTF_TYPE_VEC4)
                               ? 4
                               : 3;
                colors = ReadFloatAttribute(gltf, col_it->second, comp);
            }

            // Local -> this GenericMeshObject's shared arrays, offset by
            // whatever's already there from a previous primitive.
            int position_base = static_cast<int>(mesh_obj.positions.size());
            for (size_t i = 0; i < vertex_count; i++) {
                mesh_obj.positions.push_back({ positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2] });
            }
            int normal_base = static_cast<int>(mesh_obj.normals.size());
            for (size_t i = 0; i * 3 < normals.size(); i++) {
                mesh_obj.normals.push_back({ normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2] });
            }
            int uv_base = static_cast<int>(mesh_obj.uvs.size());
            for (size_t i = 0; i * 2 < uvs.size(); i++) {
                mesh_obj.uvs.push_back({ uvs[i * 2 + 0], uvs[i * 2 + 1] });
            }

            std::vector<uint32_t> indices = ReadIndices(gltf, prim.indices);
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                GenericFace face;
                face.num_verts = 3;
                for (int k = 0; k < 3; k++) {
                    uint32_t vi = indices[i + k];
                    face.corners[k].position_idx = position_base + static_cast<int>(vi);
                    face.corners[k].normal_idx = !normals.empty() ? normal_base + static_cast<int>(vi) : -1;
                    face.corners[k].uv_idx = !uvs.empty() ? uv_base + static_cast<int>(vi) : -1;
                }
                if (!colors.empty()) {
                    int comp = static_cast<int>(colors.size() / vertex_count);
                    MeshVec3 sum{ 0, 0, 0 };
                    for (int k = 0; k < 3; k++) {
                        uint32_t vi = indices[i + k];
                        sum.x += colors[vi * comp + 0];
                        sum.y += colors[vi * comp + 1];
                        sum.z += colors[vi * comp + 2];
                    }
                    face.has_color = true;
                    face.color = { sum.x / 3.0f, sum.y / 3.0f, sum.z / 3.0f };
                }
                mesh_obj.faces.push_back(face);
            }
        }

        if (!mesh_obj.faces.empty()) result.push_back(std::move(mesh_obj));
    }

    return result;
}

} // namespace

TmdObjImport::ImportReport Analyze(const std::string& path, const tmd::TMD_Model& model) {
    TmdObjImport::ImportReport report;
    tinygltf::Model gltf;
    if (!LoadGltf(path, gltf, report.error)) return report;

    auto meshes = BuildGenericMeshObjects(gltf);
    auto matched_by_geometry = ResolveUnmatchedObjects(meshes, model);
    for (size_t i = 0; i < meshes.size(); i++) {
        const auto& mesh = meshes[i];
        MeshImportStats stats;
        stats.object_index = mesh.object_index;
        stats.exists_in_model = mesh.object_index >= 0 && mesh.object_index < static_cast<int>(model.objects.size());
        stats.matched_by_geometry = matched_by_geometry[i];
        stats.face_count = static_cast<int>(mesh.faces.size());
        stats.vertex_count = static_cast<int>(mesh.positions.size());
        report.objects.push_back(stats);
    }
    report.ok = true;
    return report;
}

TmdObjImport::ImportReport Apply(const std::string& path, tmd::TMD_Model& model) {
    TmdObjImport::ImportReport report;
    tinygltf::Model gltf;
    if (!LoadGltf(path, gltf, report.error)) return report;

    auto meshes = BuildGenericMeshObjects(gltf);
    auto matched_by_geometry = ResolveUnmatchedObjects(meshes, model);
    for (size_t i = 0; i < meshes.size(); i++) {
        auto& mesh = meshes[i];
        MeshImportStats stats;
        stats.object_index = mesh.object_index;
        stats.exists_in_model = mesh.object_index >= 0 && mesh.object_index < static_cast<int>(model.objects.size());
        stats.matched_by_geometry = matched_by_geometry[i];
        if (stats.exists_in_model) {
            model.objects[mesh.object_index] = RebuildTmdObject(mesh, stats);
        }
        report.objects.push_back(stats);
    }
    report.ok = true;
    return report;
}

} // namespace gfx::TmdGltfImport
