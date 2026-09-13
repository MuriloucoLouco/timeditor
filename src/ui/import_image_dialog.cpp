#include "import_image_dialog.h"
#include "imgui.h"
#include "../gfx/image_quantizer.h"
#include "../gfx/tim_texture_builder.h"
#include "file_dialog.h"
#include <GL/gl.h>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace ui {

namespace {
int BppForIndex(int index) {
    switch (index) {
        case 0: return 4;
        case 1: return 8;
        case 3: return 24;
        default: return 16;
    }
}
} // namespace

void ImportImageDialog::Open(int index) {
    target_index = index;
    should_open = true;
    source_rgba.clear();
    source_width = source_height = 0;
    status_message.clear();
    preview_dirty = true;
}

void ImportImageDialog::PickSourceFile() {
    std::string path = FileDialog::OpenFile("Choose an image to import",
                                             { { "Image Files", "png,jpg,jpeg,bmp,tga,gif" } });
    if (path.empty()) return;

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        status_message = "Failed to load that image.";
        return;
    }

    source_path = path;
    source_width = w;
    source_height = h;
    source_rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);

    import_width = w;
    import_height = h;
    fit_mode = false;
    preview_dirty = true;
    status_message.clear();
}

std::vector<uint8_t> ImportImageDialog::BuildImportBuffer() const {
    int w = std::max(1, import_width);
    int h = std::max(1, import_height);
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4, 0);
    if (source_rgba.empty() || source_width <= 0 || source_height <= 0) return out;

    if (fit_mode) {
        // Nearest-neighbor resize to exactly w x h.
        for (int y = 0; y < h; y++) {
            int sy = std::min((y * source_height) / h, source_height - 1);
            for (int x = 0; x < w; x++) {
                int sx = std::min((x * source_width) / w, source_width - 1);
                for (int c = 0; c < 4; c++) {
                    out[(static_cast<size_t>(y) * w + x) * 4 + c] =
                        source_rgba[(static_cast<size_t>(sy) * source_width + sx) * 4 + c];
                }
            }
        }
    } else {
        // Crop/pad anchored top-left; anything beyond the source stays transparent.
        int copy_w = std::min(w, source_width);
        int copy_h = std::min(h, source_height);
        for (int y = 0; y < copy_h; y++) {
            for (int x = 0; x < copy_w; x++) {
                for (int c = 0; c < 4; c++) {
                    out[(static_cast<size_t>(y) * w + x) * 4 + c] =
                        source_rgba[(static_cast<size_t>(y) * source_width + x) * 4 + c];
                }
            }
        }
    }
    return out;
}

void ImportImageDialog::RegeneratePreview() {
    if (source_rgba.empty()) return;
    std::vector<uint8_t> buffer = BuildImportBuffer();
    int w = std::max(1, import_width), h = std::max(1, import_height);
    int bpp = BppForIndex(bpp_index);
    std::vector<uint8_t> preview_rgba;
    preview_palette.clear();

    if (bpp == 4 || bpp == 8) {
        int color_count = bpp == 4 ? 16 : 256;
        gfx::ImageQuantizer::Result q = gfx::ImageQuantizer::Quantize(buffer, w, h, color_count);
        preview_palette = q.palette;
        preview_rgba.assign(static_cast<size_t>(w) * h * 4, 0);
        for (size_t p = 0; p < q.indices.size(); p++) {
            uint8_t rgba[4];
            gfx::ImageQuantizer::BGR555ToRGBA(q.palette[q.indices[p]], rgba);
            for (int c = 0; c < 4; c++) preview_rgba[p * 4 + c] = rgba[c];
        }
    } else if (bpp == 16) {
        // Round-trip through BGR555 so the preview shows the real color loss.
        preview_rgba = buffer;
        for (size_t p = 0; p < static_cast<size_t>(w) * h; p++) {
            uint16_t word = gfx::ImageQuantizer::RGBAToBGR555(buffer[p * 4 + 0], buffer[p * 4 + 1], buffer[p * 4 + 2]);
            uint8_t rgba[4];
            gfx::ImageQuantizer::BGR555ToRGBA(word, rgba);
            preview_rgba[p * 4 + 0] = rgba[0];
            preview_rgba[p * 4 + 1] = rgba[1];
            preview_rgba[p * 4 + 2] = rgba[2];
            preview_rgba[p * 4 + 3] = buffer[p * 4 + 3] < 128 ? 0 : 255;
        }
    } else { // 24bpp: full RGB, no color-depth loss
        preview_rgba = buffer;
    }

    if (preview_texture == 0) glGenTextures(1, &preview_texture);
    glBindTexture(GL_TEXTURE_2D, preview_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, preview_rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    preview_dirty = false;
}

void ImportImageDialog::ApplyToTarget(tim::Document& document) {
    if (target_index < 0 || target_index >= static_cast<int>(document.Images().size())) return;
    TIM_Image& tim = document.Images()[target_index];
    document.PushContentUndoSnapshot(target_index);

    std::vector<uint8_t> buffer = BuildImportBuffer();
    int src_w = std::max(1, import_width), src_h = std::max(1, import_height);
    int bpp = BppForIndex(bpp_index);

    int px_width = gfx::ImageQuantizer::RoundWidthForBpp(src_w, bpp);
    std::vector<uint8_t> padded;
    const std::vector<uint8_t>* rgba_ptr = &buffer;
    if (px_width != src_w) {
        padded.assign(static_cast<size_t>(px_width) * src_h * 4, 0);
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                for (int c = 0; c < 4; c++) {
                    padded[(static_cast<size_t>(y) * px_width + x) * 4 + c] =
                        buffer[(static_cast<size_t>(y) * src_w + x) * 4 + c];
                }
            }
        }
        rgba_ptr = &padded;
    }
    const std::vector<uint8_t>& final_rgba = *rgba_ptr;

    tim::Document::ImageContent content;
    content.type = static_cast<uint8_t>(bpp == 4 ? 0 : bpp == 8 ? 1 : bpp == 16 ? 2 : 3);
    content.bpp = bpp;
    content.pixel_width = px_width;
    content.width_words = gfx::ImageQuantizer::PixelWidthToWords(px_width, bpp);
    content.height = src_h;

    gfx::ImageQuantizer::Result quant;
    if (bpp == 4 || bpp == 8) {
        int color_count = bpp == 4 ? 16 : 256;
        quant = gfx::ImageQuantizer::Quantize(final_rgba, px_width, src_h, color_count);
        content.image_data = gfx::ImageQuantizer::PackIndexed(quant.indices, px_width, src_h, bpp);
        content.has_clut = true;
        content.colors_per_clut = color_count;
        content.num_cluts = 1;
        content.clut_data = quant.palette;
    } else {
        content.image_data = gfx::ImageQuantizer::PackDirect(final_rgba, px_width, src_h, bpp);
        content.has_clut = false;
    }
    document.ReplaceImageContent(target_index, std::move(content));

    tim::Document::MasterContent master;
    master.rgba = final_rgba;
    master.width = px_width;
    if (bpp == 4 || bpp == 8) master.index_map = quant.indices;
    document.ReplaceMasterImage(target_index, std::move(master));

    gfx::TIMTextureBuilder::RebuildTextures(tim);

    source_rgba.clear();
    ImGui::CloseCurrentPopup();
}

void ImportImageDialog::Render(tim::Document& document) {
    if (should_open) {
        ImGui::OpenPopup("Import Image");
        should_open = false;
    }

    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Import Image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (source_rgba.empty()) {
        ImGui::TextWrapped("Choose a source image (PNG/JPG/BMP/...) to replace this TIM's content.");
        if (ImGui::Button("Choose File...")) PickSourceFile();
        if (!status_message.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", status_message.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    } else {
        std::string filename = source_path.substr(source_path.find_last_of("/\\") + 1);
        ImGui::Text("Source: %dx%d px (%s)", source_width, source_height, filename.c_str());
        if (ImGui::Button("Choose Different File...")) PickSourceFile();
        ImGui::Separator();

        ImGui::Text("Import size:");
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::InputInt("W##importw", &import_width)) preview_dirty = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::InputInt("H##importh", &import_height)) preview_dirty = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to source size")) {
            import_width = source_width;
            import_height = source_height;
            preview_dirty = true;
        }
        if (ImGui::RadioButton("Crop/Pad (anchored top-left)", !fit_mode)) { fit_mode = false; preview_dirty = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Resize (stretch)", fit_mode)) { fit_mode = true; preview_dirty = true; }
        ImGui::Separator();

        const char* bpp_labels[] = { "4 BPP (16 colors)", "8 BPP (256 colors)", "16 BPP (direct color)",
                                      "24 BPP (true color)" };
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("Import as", &bpp_index, bpp_labels, 4)) preview_dirty = true;
        if (preview_dirty) RegeneratePreview();

        ImGui::Text("Preview:");
        float preview_h = 160.0f;
        float aspect = import_height > 0 ? static_cast<float>(import_width) / import_height : 1.0f;
        ImGui::Image((void*)(intptr_t)preview_texture, ImVec2(preview_h * aspect, preview_h));

        if (!preview_palette.empty()) {
            ImGui::Text("Palette (%d colors):", static_cast<int>(preview_palette.size()));
            ImGuiStyle& style = ImGui::GetStyle();
            float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            for (int i = 0; i < static_cast<int>(preview_palette.size()); i++) {
                uint8_t rgba[4];
                gfx::ImageQuantizer::BGR555ToRGBA(preview_palette[i], rgba);
                ImGui::PushID(i);
                ImGui::ColorButton("##swatch",
                                    ImVec4(rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, 1.0f),
                                    ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
                ImGui::PopID();
                float next_x2 = ImGui::GetItemRectMax().x + style.ItemSpacing.x + 14.0f;
                if (i + 1 < static_cast<int>(preview_palette.size()) && next_x2 < window_visible_x2) {
                    ImGui::SameLine();
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Apply")) ApplyToTarget(document);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            source_rgba.clear();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

} // namespace ui
