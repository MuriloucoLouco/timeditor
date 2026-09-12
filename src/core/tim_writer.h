#pragma once
#include "tim_format.h"
#include <string>
#include <vector>

namespace tim {

// Serializes TIM_Image structs back into the on-disk PS1 TIM layout.
class Writer {
public:
    // Writes `images` concatenated, in the given order, overwriting filepath.
    static bool WriteToFile(const std::string& filepath, const std::vector<const TIM_Image*>& images);

private:
    static void SerializeImage(const TIM_Image& img, std::vector<uint8_t>& out);
};

} // namespace tim
