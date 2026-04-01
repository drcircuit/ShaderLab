#include "ShaderLab/UI/ShaderLabIDE.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace ShaderLab {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path GetLegacySnippetBaseDir(const std::string& appRoot) {
    fs::path baseDir;
    char* appData = nullptr;
    size_t appDataLen = 0;
    if (_dupenv_s(&appData, &appDataLen, "APPDATA") == 0 && appData && *appData) {
        baseDir = fs::path(appData) / "ShaderLab";
    } else {
        baseDir = fs::path(appRoot) / ".shaderlab";
    }
    if (appData) {
        free(appData);
    }
    return baseDir;
}

fs::path GetGlobalSnippetDirectory(const std::string& workspaceRoot, const std::string& appRoot) {
    if (!workspaceRoot.empty()) {
        return fs::path(workspaceRoot) / "snippets";
    }
    return GetLegacySnippetBaseDir(appRoot) / "snippets";
}

fs::path GetSnippetBackupDirectory(const std::string& appRoot) {
    return fs::path(appRoot) / "editor_assets" / "presets" / "snippets";
}

std::string SanitizeSnippetFileStem(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';
        result.push_back(ok ? c : '_');
    }
    if (result.empty()) {
        result = "Folder";
    }
    return result;
}
} // namespace

void ShaderLabIDE::LoadGlobalSnippets() {
    m_snippetFolders.clear();
    m_selectedSnippetFolderIndex = -1;
    m_selectedSnippetIndex = -1;
    m_nextSnippetId = 1;

    const fs::path snippetsDir = GetGlobalSnippetDirectory(m_workspaceRootPath, m_appRoot);
    m_snippetsDirectoryPath = snippetsDir.string();

    std::error_code ec;
    fs::create_directories(snippetsDir, ec);

    const fs::path backupSnippetsDir = GetSnippetBackupDirectory(m_appRoot);
    if (fs::exists(backupSnippetsDir, ec)) {
        for (const auto& entry : fs::directory_iterator(backupSnippetsDir, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }

            const fs::path source = entry.path();
            if (source.extension() != ".json") {
                continue;
            }

            const fs::path destination = snippetsDir / source.filename();
            if (!fs::exists(destination, ec)) {
                fs::copy_file(source, destination, fs::copy_options::none, ec);
            }
        }
    }

    const auto loadFromFile = [this](const fs::path& filePath, const std::string& fallbackFolderName) {
        std::ifstream in(filePath);
        if (!in.is_open()) {
            return;
        }

        json root;
        try {
            in >> root;
        } catch (...) {
            return;
        }

        if (!root.contains("snippets") || !root["snippets"].is_array()) {
            return;
        }

        ShaderSnippetFolder folder;
        folder.name = root.value("folder", fallbackFolderName);
        folder.filePath = filePath.string();

        for (const auto& item : root["snippets"]) {
            if (!item.is_object()) {
                continue;
            }

            ShaderSnippet snippet;
            snippet.name = item.value("name", std::string{});
            snippet.code = item.value("code", std::string{});

            if (snippet.name.empty() || snippet.code.empty()) {
                continue;
            }

            folder.snippets.push_back(std::move(snippet));
            m_nextSnippetId = (std::max)(m_nextSnippetId, static_cast<int>(folder.snippets.size()) + 1);
        }

        m_snippetFolders.push_back(std::move(folder));
    };

    if (fs::exists(snippetsDir)) {
        std::vector<fs::path> jsonFiles;
        for (const auto& entry : fs::directory_iterator(snippetsDir, ec)) {
            if (ec) {
                break;
            }

            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() == ".json") {
                jsonFiles.push_back(entry.path());
            }
        }

        std::sort(jsonFiles.begin(), jsonFiles.end());
        for (const auto& path : jsonFiles) {
            loadFromFile(path, path.stem().string());
        }
    }

    const fs::path legacyPath = GetLegacySnippetBaseDir(m_appRoot) / "snippets.json";
    if (m_snippetFolders.empty() && fs::exists(legacyPath)) {
        loadFromFile(legacyPath, "General");
    }

    if (m_snippetFolders.empty()) {
        ShaderSnippetFolder folder;
        folder.name = "General";
        folder.filePath = (snippetsDir / "General.json").string();
        m_snippetFolders.push_back(std::move(folder));
    }

    m_selectedSnippetFolderIndex = 0;
    if (!m_snippetFolders[0].snippets.empty()) {
        m_selectedSnippetIndex = 0;
    }
}

void ShaderLabIDE::SaveGlobalSnippets() const {
    if (m_snippetsDirectoryPath.empty()) {
        return;
    }

    const fs::path snippetsDir(m_snippetsDirectoryPath);
    std::error_code ec;
    fs::create_directories(snippetsDir, ec);

    for (const auto& folder : m_snippetFolders) {
        fs::path filePath = folder.filePath.empty()
            ? (snippetsDir / (SanitizeSnippetFileStem(folder.name) + ".json"))
            : fs::path(folder.filePath);

        json root;
        root["version"] = 1;
        root["folder"] = folder.name;
        root["snippets"] = json::array();

        for (const auto& snippet : folder.snippets) {
            if (snippet.name.empty() || snippet.code.empty()) {
                continue;
            }

            root["snippets"].push_back({
                {"name", snippet.name},
                {"code", snippet.code}
            });
        }

        std::ofstream out(filePath);
        if (!out.is_open()) {
            continue;
        }
        out << root.dump(2);
    }
}

void ShaderLabIDE::InsertSnippetIntoEditor(const std::string& snippetCode) {
    if (snippetCode.empty()) {
        return;
    }

    std::string insertText = snippetCode;
    if (!insertText.empty() && insertText.back() != '\n') {
        insertText.push_back('\n');
    }

    m_textEditor.InsertText(insertText);
    m_shaderState.text = m_textEditor.GetText();

    if (m_currentMode == UIMode::PostFX) {
        if (m_postFxSelectedIndex >= 0 && m_postFxSelectedIndex < (int)m_postFxDraftChain.size()) {
            auto& effect = m_postFxDraftChain[m_postFxSelectedIndex];
            effect.shaderCode = m_shaderState.text;
            effect.isDirty = true;
        } else if (m_computeEffectSelectedIndex >= 0 && m_computeEffectSelectedIndex < (int)m_computeEffectDraftChain.size()) {
            auto& effect = m_computeEffectDraftChain[m_computeEffectSelectedIndex];
            effect.shaderCode = m_shaderState.text;
            effect.isDirty = true;
        }
    } else {
        if (m_activeSceneIndex >= 0 && m_activeSceneIndex < (int)m_scenes.size()) {
            m_scenes[m_activeSceneIndex].shaderCode = m_shaderState.text;
            m_scenes[m_activeSceneIndex].isDirty = true;
        }
    }

    m_shaderState.status = CompileStatus::Dirty;
}

} // namespace ShaderLab
