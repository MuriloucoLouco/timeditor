#include "tim_parser.h"
#include <cstdio>

namespace tim {

bool Parser::LoadFromFile(const std::string& filepath, std::vector<TIM_Image>& out_images) {
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file) return false;

    int byte;
    int index_counter = 0;

    // O arquivo é varrido byte a byte procurando pela assinatura 0x00000010
    // (little-endian: 0x10, 0x00, 0x00, 0x00), já que múltiplas TIMs podem
    // estar concatenadas sem nenhum índice/tabela de conteúdo.
    while ((byte = fgetc(file)) != EOF) {
        if (byte != 0x10) continue;

        uint8_t next[3];
        if (fread(next, 1, 3, file) != 3 || next[0] != 0x00 || next[1] != 0x00 || next[2] != 0x00) {
            // Não era a assinatura completa: volta e continua varrendo byte a byte.
            fseek(file, -3, SEEK_CUR);
            continue;
        }

        TIM_Image img;
        if (!ReadOneImage(file, index_counter, filepath, img)) {
            // Erro de leitura (arquivo truncado no meio de uma imagem) — para de procurar.
            break;
        }

        if (img.file_index == -1) {
            // Falso-positivo da assinatura (pmode desconhecido): nada foi consumido
            // além dos 4 bytes de flag, que já foram devolvidos ao stream.
            continue;
        }

        index_counter++;
        out_images.push_back(std::move(img));
    }

    fclose(file);
    return !out_images.empty();
}

bool Parser::ReadOneImage(FILE* file, int file_index, const std::string& filepath, TIM_Image& img) {
    img.filename = filepath;
    img.file_index = file_index;
    img.header.id = 0x00000010;

    if (fread(&img.header.flag, sizeof(uint32_t), 1, file) != 1) return false;

    uint32_t pmode = img.header.flag & 0x07;
    if (pmode > 4) {
        // Modo de pixel desconhecido: provavelmente falso-positivo da assinatura.
        // Devolve os 4 bytes da flag e deixa o loop principal continuar procurando.
        fseek(file, -4, SEEK_CUR);
        img.file_index = -1; // marca como inválida para o chamador não contar como imagem
        return true;
    }

    img.has_clut = (img.header.flag & 0x08) != 0;
    img.type = static_cast<uint8_t>(pmode);

    if (img.has_clut) {
        if (fread(&img.clut_header, sizeof(TIM_CLUT_Header), 1, file) != 1) return false;

        size_t clut_elements = static_cast<size_t>(img.clut_header.colors_per_clut) * img.clut_header.num_cluts;
        img.clut_data.resize(clut_elements);
        if (clut_elements > 0 && fread(img.clut_data.data(), sizeof(uint16_t), clut_elements, file) != clut_elements) {
            return false;
        }
    }

    if (fread(&img.image_header, sizeof(TIM_Image_Header), 1, file) != 1) return false;

    // image_header.size inclui os 12 bytes do próprio cabeçalho de imagem.
    if (img.image_header.size < 12) return false;
    size_t image_data_size = img.image_header.size - 12;

    img.image_data.resize(image_data_size);
    if (image_data_size > 0 && fread(img.image_data.data(), 1, image_data_size, file) != image_data_size) {
        return false;
    }

    ComputeBppAndRealWidth(img);
    return true;
}

void Parser::ComputeBppAndRealWidth(TIM_Image& img) {
    switch (img.type) {
        case 0: img.bpp = 4;  img.real_width = img.image_header.width * 4; break;
        case 1: img.bpp = 8;  img.real_width = img.image_header.width * 2; break;
        case 2: img.bpp = 16; img.real_width = img.image_header.width; break;
        case 3: img.bpp = 24; img.real_width = static_cast<int>(img.image_header.width / 1.5f); break;
        default: img.bpp = 16; img.real_width = img.image_header.width; break; // "Mixed" tratado como fallback
    }
}

} // namespace tim
