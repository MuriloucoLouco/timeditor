#pragma once
#include <string>
#include <vector>
#include "tim_format.h"

namespace tim {

// Varre um arquivo em busca de um ou mais blocos TIM embutidos (um arquivo
// pode conter várias imagens concatenadas) e as adiciona em out_images.
class Parser {
public:
    // Retorna true se pelo menos uma imagem válida foi encontrada.
    static bool LoadFromFile(const std::string& filepath, std::vector<TIM_Image>& out_images);

private:
    // Tenta ler uma única imagem TIM a partir da posição atual do arquivo.
    // Retorna true em sucesso; em caso de erro de leitura (arquivo truncado),
    // retorna false e o chamador deve parar de procurar por mais imagens.
    static bool ReadOneImage(FILE* file, int file_index, const std::string& filepath, TIM_Image& out_image);

    static void ComputeBppAndRealWidth(TIM_Image& img);
};

} // namespace tim
