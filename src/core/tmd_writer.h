#pragma once
#include "tmd_format.h"
#include <string>

namespace tmd {

class Writer {
public:
    static bool WriteToFile(const std::string& filepath, const TMD_Model& model);
};

} // namespace tmd
