#pragma once
#include "tmd_format.h"
#include <string>

namespace tmd {

class Parser {
public:
    static bool LoadFromFile(const std::string& filepath, TMD_Model& out_model);
};

} // namespace tmd
