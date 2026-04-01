#pragma once

#include <string>
#include <vector>
#if !SHADERLAB_TINY_PLAYER
#include <map>
#endif
#include <cstdint>

namespace ShaderLab {

struct PackedEntry {
    std::string path; // Relative path stored in pack
    uint64_t offset;  // Offset from start of PACK segment
    uint64_t size;
};

class PackageManager {
public:
    static PackageManager& Get();

    bool Initialize(); // Analyze current executable
    bool IsPacked() const { return m_isPacked; }

    // Returns empty vector if not found or error
    std::vector<uint8_t> GetFile(const std::string& path);
    bool HasFile(const std::string& path) const;

private:
    PackageManager() = default;

    bool m_initialized = false;
    bool m_isPacked = false;
    std::string m_exePath;
    uint64_t m_packRequestOffset = 0; // Where the pack data starts in the file
#if SHADERLAB_TINY_PLAYER
    std::vector<PackedEntry> m_directory;
#else
    std::map<std::string, PackedEntry> m_directory;
#endif
};

}
