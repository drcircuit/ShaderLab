#include "ShaderLab/Core/PackageManager.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>
#include "ShaderLab/Core/ShaderLabData.h" // For nlohmann or similar if needed? No, binary format.
#include <vector>

namespace ShaderLab {

// Magic Footer: SHADERLAB_PACK
// Followed by uint64 table_offset
// Followed by magic again? 
// Let's use a simple format:
// [ ... EXE DATA ... ]
// [ PACK DATA ... ]
// [ DIRECTORY JSON STRING SIZE (uint32) ]
// [ DIRECTORY JSON STRING ]
// [ "SHADERLAB_PACK" (14 chars) ]

static const char MAGIC[] = "SHADERLAB_PACK";
static const size_t MAGIC_LEN = 14;
static const char COMPRESSED_MAGIC[] = {'S', 'L', 'Z', '1'};

typedef LONG (WINAPI *RtlDecompressBufferFn)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG);

static bool TryDecompressPackedData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    output.clear();
    constexpr size_t kHeaderSize = 12;

    if (input.size() < kHeaderSize) {
        return false;
    }
    if (std::memcmp(input.data(), COMPRESSED_MAGIC, 4) != 0) {
        return false;
    }

    uint32_t rawSize = 0;
    uint32_t compressedSize = 0;
    std::memcpy(&rawSize, input.data() + 4, sizeof(uint32_t));
    std::memcpy(&compressedSize, input.data() + 8, sizeof(uint32_t));

    if (rawSize == 0 || compressedSize == 0) {
        return false;
    }
    if (static_cast<size_t>(compressedSize) + kHeaderSize != input.size()) {
        return false;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        ntdll = LoadLibraryW(L"ntdll.dll");
    }
    if (!ntdll) {
        return false;
    }

    const auto rtlDecompressBuffer = reinterpret_cast<RtlDecompressBufferFn>(
        GetProcAddress(ntdll, "RtlDecompressBuffer"));
    if (!rtlDecompressBuffer) {
        return false;
    }

    output.resize(static_cast<size_t>(rawSize));
    ULONG finalRawSize = 0;
    constexpr USHORT kLznt1 = 2u;
    if (rtlDecompressBuffer(
            kLznt1,
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size()),
            reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(input.data() + kHeaderSize)),
            compressedSize,
            &finalRawSize) != 0) {
        output.clear();
        return false;
    }

    if (finalRawSize != output.size()) {
        output.clear();
        return false;
    }

    return true;
}

static bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& outBytes) {
    outBytes.clear();

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return false;
    }

    if (size.QuadPart == 0) {
        CloseHandle(file);
        return true;
    }

    const uint64_t byteCount = static_cast<uint64_t>(size.QuadPart);
    if (byteCount > static_cast<uint64_t>(SIZE_MAX)) {
        CloseHandle(file);
        return false;
    }

    outBytes.resize(static_cast<size_t>(byteCount));
#if SHADERLAB_TINY_PLAYER
    DWORD readBytes = 0;
    if (!ReadFile(file, outBytes.data(), static_cast<DWORD>(outBytes.size()), &readBytes, nullptr) || readBytes != outBytes.size()) {
        CloseHandle(file);
        outBytes.clear();
        return false;
    }
#else
    uint8_t* dst = outBytes.data();
    uint64_t remaining = byteCount;

    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<uint64_t>(0x40000000u)));
        DWORD chunkRead = 0;
        if (!ReadFile(file, dst, chunk, &chunkRead, nullptr) || chunkRead == 0) {
            CloseHandle(file);
            outBytes.clear();
            return false;
        }
        dst += chunkRead;
        remaining -= chunkRead;
    }
#endif

    CloseHandle(file);
    return true;
}

static bool ReadFileRange(const std::string& path, uint64_t offset, std::vector<uint8_t>& outBytes) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER seekPos = {};
    seekPos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(file);
        return false;
    }

#if SHADERLAB_TINY_PLAYER
    DWORD readBytes = 0;
    if (!ReadFile(file, outBytes.data(), static_cast<DWORD>(outBytes.size()), &readBytes, nullptr) || readBytes != outBytes.size()) {
        CloseHandle(file);
        return false;
    }
#else
    uint8_t* dst = outBytes.data();
    uint64_t remaining = static_cast<uint64_t>(outBytes.size());
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<uint64_t>(0x40000000u)));
        DWORD chunkRead = 0;
        if (!ReadFile(file, dst, chunk, &chunkRead, nullptr) || chunkRead == 0) {
            CloseHandle(file);
            return false;
        }
        dst += chunkRead;
        remaining -= chunkRead;
    }
#endif

    CloseHandle(file);
    return true;
}

PackageManager& PackageManager::Get() {
    alignas(PackageManager) static unsigned char storage[sizeof(PackageManager)] = {};
    static PackageManager* instance = nullptr;
    if (!instance) {
        instance = new (storage) PackageManager();
    }
    return *instance;
}

bool PackageManager::Initialize() {
    if (m_initialized && m_isPacked && !m_directory.empty()) {
        return true;
    }

    m_directory.clear();
    m_isPacked = false;
    m_initialized = false;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    m_exePath = exePath;

    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(m_exePath, bytes)) {
        return false;
    }

    if (bytes.size() < MAGIC_LEN + sizeof(uint64_t) + sizeof(uint32_t)) {
        return false;
    }

    const size_t fileSize = bytes.size();
    const size_t magicOffset = fileSize - MAGIC_LEN;
    if (std::memcmp(bytes.data() + magicOffset, MAGIC, MAGIC_LEN) != 0) {
        return false;
    }

    const size_t dirOffsetPos = fileSize - MAGIC_LEN - sizeof(uint64_t);
    uint64_t dirOffset = 0;
    std::memcpy(&dirOffset, bytes.data() + dirOffsetPos, sizeof(uint64_t));

    // Validate offset
    if (dirOffset >= fileSize) {
        return false;
    }

    m_isPacked = true;
    m_packRequestOffset = 0; // Not strictly needed if absolute offsets used.

    size_t cursor = static_cast<size_t>(dirOffset);
    if (cursor + sizeof(uint32_t) > fileSize) {
        return false;
    }

    uint32_t count = 0;
    std::memcpy(&count, bytes.data() + cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    for(uint32_t i=0; i<count; ++i) {
        PackedEntry entry;
        if (cursor + sizeof(uint32_t) > fileSize) {
            break;
        }
        uint32_t pathLen = 0;
        std::memcpy(&pathLen, bytes.data() + cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);
        if (pathLen > 1024) {
            break; // Sanity
        }
        if (cursor + pathLen + sizeof(uint64_t) + sizeof(uint64_t) > fileSize) {
            break;
        }
        std::vector<char> pathBuf(pathLen + 1);
        std::memcpy(pathBuf.data(), bytes.data() + cursor, pathLen);
        cursor += pathLen;
        pathBuf[pathLen] = 0;
        entry.path = pathBuf.data();
        
        std::memcpy(&entry.offset, bytes.data() + cursor, sizeof(uint64_t));
        cursor += sizeof(uint64_t);
        std::memcpy(&entry.size, bytes.data() + cursor, sizeof(uint64_t));
        cursor += sizeof(uint64_t);
        
        bool replaced = false;
#if SHADERLAB_TINY_PLAYER
        for (auto& existing : m_directory) {
            if (existing.path == entry.path) {
                existing = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_directory.push_back(entry);
        }
#else
        m_directory[entry.path] = entry;
#endif
    }

    m_initialized = true;
    return true;
}

bool PackageManager::HasFile(const std::string& path) const {
    // Normalize path separators if needed?
    // Start simple.
    std::string safePath = path; 
    std::replace(safePath.begin(), safePath.end(), '\\', '/');
#if SHADERLAB_TINY_PLAYER
    for (const auto& entry : m_directory) {
        if (entry.path == safePath) {
            return true;
        }
    }
    return false;
#else
    return m_directory.find(safePath) != m_directory.end();
#endif
}

std::vector<uint8_t> PackageManager::GetFile(const std::string& path) {
    if (!m_isPacked) return {};
    
    std::string safePath = path; 
    std::replace(safePath.begin(), safePath.end(), '\\', '/');

    PackedEntry entry;
    bool found = false;
#if SHADERLAB_TINY_PLAYER
    for (const auto& current : m_directory) {
        if (current.path == safePath) {
            entry = current;
            found = true;
            break;
        }
    }
#else
    auto it = m_directory.find(safePath);
    if (it != m_directory.end()) {
        entry = it->second;
        found = true;
    }
#endif
    if (!found) {
        return {};
    }

    if (entry.size > static_cast<uint64_t>(SIZE_MAX)) {
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(entry.size));
    if (!ReadFileRange(m_exePath, entry.offset, data)) {
        return {};
    }

    std::vector<uint8_t> decompressed;
    if (TryDecompressPackedData(data, decompressed)) {
        return decompressed;
    }

    return data;
}

}
