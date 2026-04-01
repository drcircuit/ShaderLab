#pragma once

#include <string>

#include "ShaderLab/Core/ShaderLabData.h"

namespace ShaderLab {

struct RuntimeExportRequest {
    std::string appRoot;
    std::string destExePath;
    ProjectData data;
};

struct RuntimeExportResult {
    bool success = false;
    std::string message;
};

class RuntimeExporter {
public:
    static RuntimeExportResult Export(const RuntimeExportRequest& request);
};

} // namespace ShaderLab
