#include "tmd_object_renderer.h"
#include "tmd_space.h"
#include <GL/gl.h>
#include <algorithm>
#include <cmath>

namespace gfx::TmdObjectRenderer {

namespace {

// gfx::ToViewerSpace (tmd_space.h) is visible here unqualified - this
// namespace nests inside gfx.

// Ambient 0.55 + up to 0.75 of diffuse from a fixed, pleasant three-quarter
// light. There's no lighting setup stored in a TMD (the real GTE light
// matrix is supplied by the game at runtime) - this is a stand-in so lit
// polygons still show believable shading rather than a flat color.
float LightFactor(Vec3 world_normal) {
    static const Vec3 kLightDir = Vec3{ 0.4f, 0.8f, 0.5f }.Normalized();
    float n_dot_l = std::max(0.0f, world_normal.Normalized().Dot(kLightDir));
    return 0.55f + 0.75f * n_dot_l;
}

uint8_t ClampByte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); }

} // namespace

void DrawObject(const tmd::TMD_Object& obj, const Mat4& model_matrix, const VRAMManager& vram_manager,
                 TmdTextureCache& texture_cache, const Options& options) {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMultMatrixf(model_matrix.m);

    // glPolygonMode affects the rasterizer for *any* subsequent draw call,
    // including ImGui's own - the caller must reset this to GL_FILL before
    // returning control to ImGui's rendering regardless of this option.
    glPolygonMode(GL_FRONT_AND_BACK, options.wireframe ? GL_LINE : GL_FILL);

    for (int poly_index = 0; poly_index < static_cast<int>(obj.polygons.size()); poly_index++) {
        const auto& poly = obj.polygons[poly_index];
        bool is_highlighted = poly_index == options.highlight_primitive;
        bool use_texture = options.textured && poly.textured && !is_highlighted;
        TmdTextureCache::Tile tile;

        if (use_texture) {
            tile = texture_cache.GetTile(vram_manager, poly.tsb, poly.cba);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tile.gl_tex);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        if (options.cull_backfaces && !poly.double_sided) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }

        if (poly.semi_transparent && !is_highlighted) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }

        glBegin(poly.num_verts == 4 ? GL_TRIANGLE_STRIP : GL_TRIANGLES);
        for (int i = 0; i < poly.num_verts; i++) {
            uint16_t vi = poly.vert_idx[i];
            Vec3 pos = (vi < obj.vertices.size()) ? ToViewerSpace(obj.vertices[vi].x, obj.vertices[vi].y, obj.vertices[vi].z)
                                                   : Vec3{ 0, 0, 0 };

            if (is_highlighted) {
                glColor4ub(40, 255, 40, 255);
                glVertex3f(pos.x, pos.y, pos.z);
                continue;
            }

            float light = 1.0f;
            if (!poly.no_light) {
                uint16_t ni = poly.norm_idx[i];
                Vec3 obj_normal = (ni != tmd::TMD_Polygon::kNoNormal && ni < obj.normals.size())
                                       ? ToViewerSpace(obj.normals[ni].x, obj.normals[ni].y, obj.normals[ni].z)
                                       : Vec3{ 0, 1, 0 };
                Vec3 world_normal = TransformDirection(model_matrix, obj_normal);
                light = LightFactor(world_normal);
            }

            // Untextured (or "plain white" mode): color[] is the polygon's
            // own literal 0-255 surface color, multiplied by light unless
            // no_light. Textured: color[] (when present at all - see
            // tmd_parser.cpp) is a 128-centered tint over the texture texel
            // that GL_MODULATE multiplies in below, so it's rescaled to
            // "255 == neutral" first; 128,128,128 is this struct's default,
            // giving a neutral tint when a mode has no tint field at all.
            uint8_t r, g, b;
            if (!options.textured) {
                r = g = b = ClampByte(255.0f * light);
            } else if (use_texture) {
                float lit = poly.no_light ? 1.0f : light;
                r = ClampByte(255.0f * (poly.color[i][0] / 128.0f) * lit);
                g = ClampByte(255.0f * (poly.color[i][1] / 128.0f) * lit);
                b = ClampByte(255.0f * (poly.color[i][2] / 128.0f) * lit);
            } else {
                float lit = poly.no_light ? 1.0f : light;
                r = ClampByte(poly.color[i][0] * lit);
                g = ClampByte(poly.color[i][1] * lit);
                b = ClampByte(poly.color[i][2] * lit);
            }

            if (options.selected_polygons && poly_index < static_cast<int>(options.selected_polygons->size()) &&
                (*options.selected_polygons)[poly_index]) {
                constexpr float kTint = 0.55f;
                r = ClampByte(r * (1.0f - kTint) + 255.0f * kTint);
                g = ClampByte(g * (1.0f - kTint) + 140.0f * kTint);
                b = ClampByte(b * (1.0f - kTint) + 0.0f * kTint);
            }

            glColor4ub(r, g, b, poly.semi_transparent ? 200 : 255);
            if (use_texture) {
                float tex_w = static_cast<float>(std::max(1, tile.width));
                glTexCoord2f(poly.u[i] / tex_w, poly.v[i] / 256.0f);
            }
            glVertex3f(pos.x, pos.y, pos.z);
        }
        glEnd();
    }

    glPopMatrix();
}

void DrawSelectionOverlay(const Mat4& model_matrix, const std::vector<OverlayPoint>& points,
                          const std::vector<OverlayLine>& lines) {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMultMatrixf(model_matrix.m);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE); // never punches into the depth buffer for what's drawn after

    if (!lines.empty()) {
        glLineWidth(1.5f);
        glBegin(GL_LINES);
        for (const auto& l : lines) {
            if (l.selected) glColor4ub(255, 140, 0, 255);
            else glColor4ub(255, 255, 255, 180);
            glVertex3f(l.a.x, l.a.y, l.a.z);
            glVertex3f(l.b.x, l.b.y, l.b.z);
        }
        glEnd();
    }

    if (!points.empty()) {
        glPointSize(6.0f);
        glBegin(GL_POINTS);
        for (const auto& p : points) {
            if (p.selected) glColor4ub(255, 165, 0, 255);
            else glColor4ub(20, 20, 20, 255);
            glVertex3f(p.position.x, p.position.y, p.position.z);
        }
        glEnd();
    }

    glDepthMask(GL_TRUE);
    glPopMatrix();
}

} // namespace gfx::TmdObjectRenderer
