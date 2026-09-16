#include "tmd_parser.h"
#include "tmd_bits.h"
#include "log.h"
#include <cstdio>
#include <vector>

namespace tmd {

namespace {

using namespace tmd::bits;

// Little-endian cursor over an in-memory copy of the file, with bounds
// checks so a truncated/malformed TMD can't read out of the buffer -
// out-of-range reads just return 0 and leave the cursor to keep advancing.
struct Cursor {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    bool CanRead(size_t n) const { return pos + n <= size; }

    uint8_t U8() {
        uint8_t v = CanRead(1) ? data[pos] : 0;
        pos += 1;
        return v;
    }
    uint16_t U16() {
        uint16_t v = CanRead(2) ? static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8)) : 0;
        pos += 2;
        return v;
    }
    uint32_t U32() {
        uint32_t v = CanRead(4) ? static_cast<uint32_t>(data[pos] | (data[pos + 1] << 8) |
                                                         (data[pos + 2] << 16) | (data[pos + 3] << 24))
                                 : 0;
        pos += 4;
        return v;
    }
};

// Reads `count` u16 values packed 2-per-dword (PS-X's usual vertex-index
// packing), consuming a trailing pad u16 if `count` is odd so the cursor
// always lands back on a dword boundary.
void ReadPackedU16(Cursor& body, uint16_t* out, int count) {
    int i = 0;
    while (i < count) {
        out[i] = body.U16();
        if (i + 1 < count) {
            out[i + 1] = body.U16();
        } else {
            body.U16(); // pad
        }
        i += 2;
    }
}

// Decodes one polygon primitive's body (the `ilen*4` bytes following its
// 4-byte header) into a unified TMD_Polygon. This isn't a lookup table of
// the ~20 named structs in tmd.h - it derives the exact same byte layout
// generically from the mode/flag bits, verified dword-for-dword against
// every combination documented in TMD.md and tmd.h:
//
//   num_verts = quad ? 4 : 3
//   has_color = !textured || no_light
//   num_colors = !has_color        ? 0
//              : no_light          ? (gouraud ? num_verts : 1)
//              : /* lit */           (gradation ? num_verts : 1)
//   has_normal = !no_light
//   num_normals = !has_normal ? 0 : (gouraud ? num_verts : 1)
//
// Layout order is: [UV+CBA+TSB per vertex, if textured] [colors]
// [normal+vertex indices - interleaved per vertex if gouraud, else one
// shared normal followed by all vertex indices packed 2-per-dword].
//
// The caller always advances by the file's own `ilen` afterward rather
// than trusting how many bytes this function consumed, so an
// unanticipated combination can never desync the rest of the primitive
// table even if decoded imperfectly.
void DecodePolygon(Cursor& body, uint8_t mode, uint8_t flag, TMD_Polygon& poly) {
    bool textured = (mode & kModeTextured) != 0;
    bool quad = (mode & kModeQuad) != 0;
    bool gouraud = (mode & kModeGouraud) != 0;
    bool no_light = (mode & kModeNoLight) != 0;

    poly.num_verts = quad ? 4 : 3;
    poly.textured = textured;
    poly.gouraud = gouraud;
    poly.no_light = no_light;
    poly.double_sided = (flag & kFlagDoubleSided) != 0;
    poly.semi_transparent = (mode & kModeSemiTransparent) != 0;

    int num_verts = poly.num_verts;

    if (textured) {
        for (int i = 0; i < num_verts; i++) {
            poly.u[i] = body.U8();
            poly.v[i] = body.U8();
            uint16_t aux = body.U16();
            if (i == 0) poly.cba = aux;
            else if (i == 1) poly.tsb = aux;
        }
    }

    bool has_color = !textured || no_light;
    int num_colors = 0;
    if (has_color) {
        bool gradation = (flag & kFlagGradation) != 0;
        num_colors = no_light ? (gouraud ? num_verts : 1) : (gradation ? num_verts : 1);
    }
    for (int i = 0; i < num_colors; i++) {
        uint8_t r = body.U8(), g = body.U8(), b = body.U8();
        body.U8(); // mode2/pad
        poly.color[i][0] = r;
        poly.color[i][1] = g;
        poly.color[i][2] = b;
    }
    poly.color_per_vertex = num_colors > 1;
    if (num_colors == 1) {
        for (int k = 1; k < num_verts; k++) {
            poly.color[k][0] = poly.color[0][0];
            poly.color[k][1] = poly.color[0][1];
            poly.color[k][2] = poly.color[0][2];
        }
    }

    bool has_normal = !no_light;
    if (has_normal && gouraud) {
        for (int i = 0; i < num_verts; i++) {
            poly.norm_idx[i] = body.U16();
            poly.vert_idx[i] = body.U16();
        }
    } else if (has_normal) {
        uint16_t shared_norm = body.U16();
        poly.vert_idx[0] = body.U16();
        for (int i = 0; i < num_verts; i++) poly.norm_idx[i] = shared_norm;
        ReadPackedU16(body, &poly.vert_idx[1], num_verts - 1);
    } else {
        ReadPackedU16(body, poly.vert_idx, num_verts);
    }
}

} // namespace

bool Parser::LoadFromFile(const std::string& filepath, TMD_Model& out_model) {
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file) {
        core::Log::Error("Could not open TMD file: %s", filepath.c_str());
        return false;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size < 12) {
        fclose(file);
        core::Log::Error("%s is too small to be a valid TMD file.", filepath.c_str());
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
    size_t read = fread(bytes.data(), 1, bytes.size(), file);
    fclose(file);
    if (read != bytes.size()) {
        core::Log::Error("Failed reading %s (only got %zu of %zu bytes).", filepath.c_str(), read, bytes.size());
        return false;
    }

    Cursor header{ bytes.data(), bytes.size(), 0 };
    uint32_t id = header.U32();
    if (id != 0x00000041) {
        core::Log::Error("%s is not a TMD file (bad signature).", filepath.c_str());
        return false;
    }

    uint32_t flag = header.U32();
    uint32_t nobjs = header.U32();

    out_model.filename = filepath;
    out_model.flag = flag;
    out_model.objects.clear();
    out_model.objects.reserve(nobjs);

    // Object table offsets are relative to right after the 12-byte file
    // header, and are byte offsets into the file from there (not from the
    // object header itself) - see TMD.md's NormalTab example.
    constexpr size_t kFileHeaderSize = 12;
    constexpr size_t kObjHeaderSize = 28;

    for (uint32_t i = 0; i < nobjs; i++) {
        size_t obj_header_pos = kFileHeaderSize + i * kObjHeaderSize;
        if (obj_header_pos + kObjHeaderSize > bytes.size()) break;

        Cursor oh{ bytes.data(), bytes.size(), obj_header_pos };
        uint32_t vert_top = oh.U32();
        uint32_t n_vert = oh.U32();
        uint32_t norm_top = oh.U32();
        uint32_t n_norm = oh.U32();
        uint32_t prim_top = oh.U32();
        uint32_t n_prim = oh.U32();
        int32_t scale = static_cast<int32_t>(oh.U32()); // "apparently unused" per TMD.md

        TMD_Object obj;
        obj.scale = scale;

        Cursor vc{ bytes.data(), bytes.size(), kFileHeaderSize + vert_top };
        obj.vertices.reserve(n_vert);
        for (uint32_t v = 0; v < n_vert; v++) {
            TMD_Vertex vert;
            vert.x = static_cast<int16_t>(vc.U16());
            vert.y = static_cast<int16_t>(vc.U16());
            vert.z = static_cast<int16_t>(vc.U16());
            vc.U16(); // filler
            obj.vertices.push_back(vert);
        }

        Cursor nc{ bytes.data(), bytes.size(), kFileHeaderSize + norm_top };
        obj.normals.reserve(n_norm);
        for (uint32_t n = 0; n < n_norm; n++) {
            TMD_Normal norm;
            norm.x = static_cast<int16_t>(nc.U16());
            norm.y = static_cast<int16_t>(nc.U16());
            norm.z = static_cast<int16_t>(nc.U16());
            nc.U16(); // filler
            obj.normals.push_back(norm);
        }

        Cursor pc{ bytes.data(), bytes.size(), kFileHeaderSize + prim_top };
        obj.polygons.reserve(n_prim);
        for (uint32_t p = 0; p < n_prim; p++) {
            if (!pc.CanRead(4)) break;
            uint8_t olen = pc.U8();
            uint8_t ilen = pc.U8();
            uint8_t poly_flag = pc.U8();
            uint8_t poly_mode = pc.U8();

            size_t body_start = pc.pos;
            if (poly_mode & kModeIsPolygon) {
                Cursor body{ bytes.data(), bytes.size(), body_start };
                TMD_Polygon poly;
                DecodePolygon(body, poly_mode, poly_flag, poly);
                poly.olen = olen;
                obj.polygons.push_back(poly);
            }
            // Lines (mode's polygon bit clear) aren't rendered by the 3D
            // viewer; either way, always skip by the file's own ilen so an
            // unrecognized primitive can never desync the table.
            pc.pos = body_start + static_cast<size_t>(ilen) * 4;
        }

        out_model.objects.push_back(std::move(obj));
    }

    return !out_model.objects.empty();
}

} // namespace tmd
