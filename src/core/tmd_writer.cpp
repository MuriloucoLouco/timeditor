#include "tmd_writer.h"
#include "tmd_bits.h"
#include <cstdio>
#include <vector>

namespace tmd {

using namespace tmd::bits;

namespace {

void AppendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
void AppendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// Writes `count` u16 values packed 2-per-dword with a trailing pad word if
// `count` is odd - the exact inverse of tmd_parser.cpp's ReadPackedU16.
void WritePackedU16(std::vector<uint8_t>& out, const uint16_t* values, int count) {
    int i = 0;
    while (i < count) {
        AppendU16(out, values[i]);
        if (i + 1 < count) AppendU16(out, values[i + 1]);
        else AppendU16(out, 0); // pad
        i += 2;
    }
}

// Documented GPU ordering-table packet size for a primitive built fresh
// (no original file to copy TMD_Polygon::olen from) - see TMD.md/tmd.h,
// cross-checked exhaustively against every case while writing the parser.
// The lit+untextured+non-gouraud case is the only one where the
// gradation/per-vertex-color choice changes olen (F_3=4 vs F_3G=6,
// F_4=5 vs F_4G=8); every other case's olen is fixed by
// (textured, quad, gouraud, no_light) alone.
uint8_t FallbackOlen(bool textured, bool quad, bool gouraud, bool no_light, bool color_per_vertex) {
    if (!quad) {
        if (!textured) {
            if (no_light) return gouraud ? 6 : 4;
            if (gouraud) return 6;
            return color_per_vertex ? 6 : 4;
        }
        return gouraud ? 9 : 7;
    }
    if (!textured) {
        if (no_light) return gouraud ? 8 : 5;
        if (gouraud) return 8;
        return color_per_vertex ? 8 : 5;
    }
    return gouraud ? 12 : 9;
}

void EncodePolygon(const TMD_Polygon& poly, std::vector<uint8_t>& out) {
    int num_verts = poly.num_verts;

    uint8_t mode = kModeIsPolygon;
    if (poly.textured) mode |= kModeTextured;
    if (num_verts == 4) mode |= kModeQuad;
    if (poly.gouraud) mode |= kModeGouraud;
    if (poly.no_light) mode |= kModeNoLight;
    if (poly.semi_transparent) mode |= kModeSemiTransparent;

    bool has_color = !poly.textured || poly.no_light;
    // GRD only ever appears in the file for the lit (non-no_light) branch -
    // for no_light, whether colors are per-vertex is already implied by
    // `gouraud` alone (see DecodePolygon), so the flag bit is never set there.
    bool grad_flag = has_color && !poly.no_light && poly.color_per_vertex;

    uint8_t flag = 0;
    if (poly.double_sided) flag |= kFlagDoubleSided;
    if (grad_flag) flag |= kFlagGradation;

    std::vector<uint8_t> body;

    if (poly.textured) {
        for (int i = 0; i < num_verts; i++) {
            AppendU8(body, poly.u[i]);
            AppendU8(body, poly.v[i]);
            uint16_t aux = (i == 0) ? poly.cba : (i == 1) ? poly.tsb : 0;
            AppendU16(body, aux);
        }
    }

    int num_colors = has_color ? (poly.color_per_vertex ? num_verts : 1) : 0;
    for (int i = 0; i < num_colors; i++) {
        AppendU8(body, poly.color[i][0]);
        AppendU8(body, poly.color[i][1]);
        AppendU8(body, poly.color[i][2]);
        AppendU8(body, mode); // mode2: "a copy of mode, used as filler" per TMD.md
    }

    bool has_normal = !poly.no_light;
    if (has_normal && poly.gouraud) {
        for (int i = 0; i < num_verts; i++) {
            AppendU16(body, poly.norm_idx[i]);
            AppendU16(body, poly.vert_idx[i]);
        }
    } else if (has_normal) {
        AppendU16(body, poly.norm_idx[0]);
        AppendU16(body, poly.vert_idx[0]);
        WritePackedU16(body, &poly.vert_idx[1], num_verts - 1);
    } else {
        WritePackedU16(body, poly.vert_idx, num_verts);
    }

    uint8_t ilen = static_cast<uint8_t>(body.size() / 4);
    uint8_t olen = poly.olen != 0
                       ? poly.olen
                       : FallbackOlen(poly.textured, num_verts == 4, poly.gouraud, poly.no_light, poly.color_per_vertex);

    AppendU8(out, olen);
    AppendU8(out, ilen);
    AppendU8(out, flag);
    AppendU8(out, mode);
    out.insert(out.end(), body.begin(), body.end());
}

} // namespace

bool Writer::WriteToFile(const std::string& filepath, const TMD_Model& model) {
    constexpr size_t kObjHeaderSize = 28;

    // Pre-encode every object's vertex/normal/primitive bytes so the
    // per-object table offsets (relative to right after the 12-byte file
    // header, matching how the parser reads them - see TMD.md) can be
    // computed before any of it is written.
    struct EncodedObject {
        std::vector<uint8_t> verts, norms, prims;
    };
    std::vector<EncodedObject> encoded(model.objects.size());

    for (size_t i = 0; i < model.objects.size(); i++) {
        const TMD_Object& obj = model.objects[i];
        EncodedObject& enc = encoded[i];

        for (const auto& v : obj.vertices) {
            AppendU16(enc.verts, static_cast<uint16_t>(v.x));
            AppendU16(enc.verts, static_cast<uint16_t>(v.y));
            AppendU16(enc.verts, static_cast<uint16_t>(v.z));
            AppendU16(enc.verts, 0); // filler
        }
        for (const auto& n : obj.normals) {
            AppendU16(enc.norms, static_cast<uint16_t>(n.x));
            AppendU16(enc.norms, static_cast<uint16_t>(n.y));
            AppendU16(enc.norms, static_cast<uint16_t>(n.z));
            AppendU16(enc.norms, 0); // filler
        }
        for (const auto& poly : obj.polygons) {
            EncodePolygon(poly, enc.prims);
        }
    }

    std::vector<uint8_t> out;
    AppendU32(out, 0x00000041);
    AppendU32(out, model.flag);
    AppendU32(out, static_cast<uint32_t>(model.objects.size()));

    uint32_t offset = static_cast<uint32_t>(model.objects.size() * kObjHeaderSize);
    std::vector<uint32_t> vert_tops(model.objects.size()), norm_tops(model.objects.size()),
        prim_tops(model.objects.size());
    for (size_t i = 0; i < model.objects.size(); i++) {
        vert_tops[i] = offset;
        offset += static_cast<uint32_t>(encoded[i].verts.size());
        norm_tops[i] = offset;
        offset += static_cast<uint32_t>(encoded[i].norms.size());
        prim_tops[i] = offset;
        offset += static_cast<uint32_t>(encoded[i].prims.size());
    }

    for (size_t i = 0; i < model.objects.size(); i++) {
        const TMD_Object& obj = model.objects[i];
        AppendU32(out, vert_tops[i]);
        AppendU32(out, static_cast<uint32_t>(obj.vertices.size()));
        AppendU32(out, norm_tops[i]);
        AppendU32(out, static_cast<uint32_t>(obj.normals.size()));
        AppendU32(out, prim_tops[i]);
        AppendU32(out, static_cast<uint32_t>(obj.polygons.size()));
        AppendU32(out, static_cast<uint32_t>(obj.scale));
    }

    for (size_t i = 0; i < model.objects.size(); i++) {
        out.insert(out.end(), encoded[i].verts.begin(), encoded[i].verts.end());
        out.insert(out.end(), encoded[i].norms.begin(), encoded[i].norms.end());
        out.insert(out.end(), encoded[i].prims.begin(), encoded[i].prims.end());
    }

    FILE* file = fopen(filepath.c_str(), "wb");
    if (!file) return false;
    bool ok = fwrite(out.data(), 1, out.size(), file) == out.size();
    fclose(file);
    return ok;
}

} // namespace tmd
