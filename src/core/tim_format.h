#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Cabeçalho fixo de todo arquivo/bloco TIM (assinatura + flags de formato).
struct TIM_Header {
    uint32_t id;   // Sempre 0x00000010
    uint32_t flag; // bits 0-2: modo de pixel; bit 3: possui CLUT
};

// Cabeçalho da paleta (CLUT - Color Look-Up Table), presente apenas
// quando TIM_Header::flag indica que a imagem é indexada por paleta.
struct TIM_CLUT_Header {
    uint32_t size;
    uint16_t origin_x;       // Coordenada X na VRAM (Palette Org)
    uint16_t origin_y;       // Coordenada Y na VRAM (Palette Org)
    uint16_t colors_per_clut;
    uint16_t num_cluts;
};

// Cabeçalho dos dados de imagem em si.
struct TIM_Image_Header {
    uint32_t size;
    uint16_t origin_x; // Coordenada X na VRAM (Image Org)
    uint16_t origin_y; // Coordenada Y na VRAM (Image Org)
    uint16_t width;    // Largura em "words" de 16 bits (não em pixels!)
    uint16_t height;
};

// Formatos de pixel suportados pelo PS1 (campo "pmode" do TIM_Header::flag).
enum class TIMPixelMode : uint8_t {
    Indexed4BPP  = 0,
    Indexed8BPP  = 1,
    Direct16BPP  = 2,
    Direct24BPP  = 3,
    Mixed        = 4,
};

// Representa uma imagem TIM já carregada e decodificada, incluindo os
// dados prontos para virar textura (opengl_texture_ids é preenchido por
// gfx::TIMTextureBuilder, não pelo parser).
class TIM_Image {
public:
    std::string filename;
    int file_index = 0; // Índice da imagem dentro do arquivo de origem (um arquivo pode ter várias TIMs)

    TIM_Header header{};
    bool has_clut = false;
    uint8_t type = 0; // Ver TIMPixelMode

    TIM_CLUT_Header clut_header{};
    std::vector<uint16_t> clut_data; // BGR555, tamanho = colors_per_clut * num_cluts

    TIM_Image_Header image_header{};
    std::vector<uint8_t> image_data; // Bytes crus, ainda no formato empacotado do PS1

    int real_width = 0; // Largura em pixels já convertida a partir de image_header.width
    int bpp = 0;         // 4, 8, 16 ou 24

    // Preenchido por gfx::TIMTextureBuilder: uma textura OpenGL por CLUT
    // (ou uma única textura para formatos de cor direta).
    std::vector<uint32_t> opengl_texture_ids;
    int selected_clut = 0;

    bool selected = false; // Estado de seleção na UI (usado para "desenhar na VRAM")
};
