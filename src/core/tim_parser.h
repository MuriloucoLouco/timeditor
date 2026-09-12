#pragma once
#include <string>
#include <vector>
#include "tim_format.h"

namespace tim {

// Scans a file for one or more embedded TIM blocks (files can contain
// several concatenated images) and appends them to out_images.
class Parser {
public:
    // Returns true if at least one valid image was found.
    static bool LoadFromFile(const std::string& filepath, std::vector<TIM_Image>& out_images);

private:
    // Reads a single TIM image starting at the file's current position.
    // Returns false only on a read error (truncated file); the caller
    // should then stop scanning.
    static bool ReadOneImage(FILE* file, int file_index, const std::string& filepath, TIM_Image& out_image);

    static void ComputeBppAndRealWidth(TIM_Image& img);
};

} // namespace tim
