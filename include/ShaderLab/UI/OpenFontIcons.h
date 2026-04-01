#pragma once

#include <cstdint>
#include <string>

namespace ShaderLab::OpenFontIcons {

inline std::string ToUtf8(uint32_t codepoint) {
    std::string out;
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return out;
}

inline std::string Label(uint32_t codepoint, const char* id) {
    std::string out = ToUtf8(codepoint);
    out += "##";
    out += id;
    return out;
}

constexpr uint32_t kPlus = 0xE0AC;
constexpr uint32_t kMinus = 0xE091;
constexpr uint32_t kFolder = 0xE067;
constexpr uint32_t kFolderPlus = 0xE066;
constexpr uint32_t kFolderMinus = 0xE065;
constexpr uint32_t kSave = 0xE0B7;
constexpr uint32_t kDelete = 0xE0DB;
constexpr uint32_t kTrash2 = 0xE0DA;
constexpr uint32_t kInsert = 0xE081;
constexpr uint32_t kLock = 0xE080;
constexpr uint32_t kUnlock = 0xE0E4;
constexpr uint32_t kCopy = 0xE042;
constexpr uint32_t kCheck = 0xE02D;
constexpr uint32_t kRefresh = 0xE0B1;
constexpr uint32_t kChevronUp = 0xE031;
constexpr uint32_t kChevronDown = 0xE02E;
constexpr uint32_t kPlay = 0xE0A9;
constexpr uint32_t kStop = 0xE0CD;
constexpr uint32_t kSearch = 0xE0B9;
constexpr uint32_t kX = 0xE0FA;
constexpr uint32_t kXCircle = 0xE0F8;
constexpr uint32_t kFilePlus = 0xE05F;
constexpr uint32_t kCode = 0xE03F;

} // namespace ShaderLab::OpenFontIcons
